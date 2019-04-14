#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "vm.h"
#include "dict.h"
#include "array_obj.h"

#ifdef NOLOG
#define LOG(...)
#undef assert
#define assert(...)
#else
#define LOG(fmt, ...) do { printf(fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
#endif
#define FATAL(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)

// notes: architecture is big endian!

void vm_init(struct vm *vm) {
    vm->env = malloc(sizeof(struct env));
    env_init(vm->env, NULL);
    vm->code = (a_uint8)array_init(uint8_t);
    vm->stack = (a_value){
        .data = calloc(8, sizeof(struct value)),
        .length = 0,
        .capacity = 8
    };
    vm->ip = 0;
    vm->dstr = vm->dint = vm->dfloat = 0;
}

void vm_free(struct vm *vm) {
    env_free(vm->env);
    free(vm->env);

    array_free(vm->code);
    for(size_t i = 0; i < vm->stack.length; i++)
        value_free(&vm->stack.data[i]);
    array_free(vm->stack);
}

int vm_step(struct vm *vm) {
    const enum vm_opcode op = vm->code.data[vm->ip];
    switch(op) {
    case OP_HALT: {
        LOG("HALT\n");
#ifndef NOLOG
        vm_print_stack(vm);
#endif
        return 0;
    }

    // stack manip
    // push int
#define push_int_op(optype, type, _data) \
    case optype: { \
        vm->ip++; \
        const type data = _data; \
        vm->ip += sizeof(type); \
        LOG(sizeof(type) == 8 ? "PUSH %ld\n" : "PUSH %d\n", data); \
\
        array_push(vm->stack, (struct value){}); \
        value_int(&array_top(vm->stack), data); \
        break; \
    }
    push_int_op(OP_PUSH8,  uint8_t,  vm->code.data[vm->ip+0])

    push_int_op(OP_PUSH16, uint16_t, vm->code.data[vm->ip+0] << 4 |
                                     vm->code.data[vm->ip+1])

    push_int_op(OP_PUSH32, uint32_t, vm->code.data[vm->ip+0] << 12 |
                                     vm->code.data[vm->ip+1] << 8  |
                                     vm->code.data[vm->ip+2] << 4  |
                                     vm->code.data[vm->ip+3])

    push_int_op(OP_PUSH64, uint64_t, vm->code.data[vm->ip+0] << 28 |
                                     vm->code.data[vm->ip+1] << 24 |
                                     vm->code.data[vm->ip+2] << 20 |
                                     vm->code.data[vm->ip+3] << 16 |
                                     vm->code.data[vm->ip+4] << 12 |
                                     vm->code.data[vm->ip+5] << 8  |
                                     vm->code.data[vm->ip+6] << 4  |
                                     vm->code.data[vm->ip+7])

    // push float
    case OP_PUSHF32: {
        vm->ip++;
        union {
            float f;
            uint8_t u[4];
        } u;
        u.u[0] = vm->code.data[vm->ip+0];
        u.u[1] = vm->code.data[vm->ip+1];
        u.u[2] = vm->code.data[vm->ip+2];
        u.u[3] = vm->code.data[vm->ip+3];
        vm->ip += sizeof(u);
        LOG("PUSH_F32 %f\n", u.f);
        array_push(vm->stack, (struct value){});
        value_float(&array_top(vm->stack), u.f);
        break;
    }
    case OP_PUSHF64: {
        vm->ip++;
        union {
            double d;
            uint8_t u[4];
        } u;
        u.u[0] = vm->code.data[vm->ip+0];
        u.u[1] = vm->code.data[vm->ip+1];
        u.u[2] = vm->code.data[vm->ip+2];
        u.u[3] = vm->code.data[vm->ip+3];
        u.u[4] = vm->code.data[vm->ip+4];
        u.u[5] = vm->code.data[vm->ip+5];
        u.u[6] = vm->code.data[vm->ip+6];
        u.u[7] = vm->code.data[vm->ip+7];
        vm->ip += sizeof(u);
        LOG("PUSH_F64 %f\n", u.d);
        array_push(vm->stack, (struct value){});
        value_float(&array_top(vm->stack), u.d);
        break;
    }

    // pushstr
    case OP_PUSHSTR: {
        vm->ip++;
        char *str = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(str)+1;
        LOG("PUSH %s\n", str);
        array_push(vm->stack, (struct value){});
        value_str(&array_top(vm->stack), str);
        break;
    }

    // nil
    case OP_PUSH_NIL: {
        vm->ip++;
        LOG("PUSH NIL\n");
        array_push(vm->stack, (struct value){});
        break;
    }

    // pop
    case OP_POP: {
        LOG("POP\n");
        assert(vm->stack.length > 0);
        vm->ip++;
        value_free(&array_top(vm->stack));
        array_pop(vm->stack);
        break;
    }

    // unary
    case OP_NOT: {
        vm->ip++;
        struct value val = array_top(vm->stack);
        array_pop(vm->stack);
        int truth = value_is_true(&val);
        value_free(&val);
        value_int(&val, !truth);
        array_push(vm->stack, val);
        break;
    }
    case OP_NEGATE: {
        vm->ip++;
        struct value *val = &array_top(vm->stack);
        if(val->type == TYPE_INT) {
            val->as.integer = -val->as.integer;
        } else if(val->type == TYPE_FLOAT) {
            val->as.floatp = -val->as.floatp;
        }
        break;
    }

    // arith
#define binop(optype, fn) \
    case optype: { \
        LOG("" #optype "\n"); \
        assert(vm->stack.length >= 2); \
        vm->ip++; \
\
        struct value right = array_top(vm->stack); \
        array_pop(vm->stack); \
        struct value left = array_top(vm->stack); \
        array_pop(vm->stack); \
\
        array_push(vm->stack, (struct value){}); \
        struct value *result = &array_top(vm->stack); \
        fn(result, &left, &right); \
        value_free(&left); value_free(&right); \
        break; \
    }
    binop(OP_ADD, value_add)
    binop(OP_SUB, value_sub)
    binop(OP_MUL, value_mul)
    binop(OP_DIV, value_div)
    binop(OP_MOD, value_mod)

    // logic
    binop(OP_AND, value_and)
    binop(OP_OR, value_or)

    // comparison
    binop(OP_LT,  value_lt)
    binop(OP_LEQ, value_leq)
    binop(OP_GT,  value_gt)
    binop(OP_GEQ, value_geq)
    binop(OP_EQ,  value_eq)
    binop(OP_NEQ, value_neq)

    // variables
    case OP_SET: {
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        LOG("SET %s\n", key);
        env_set(vm->env, key, &array_top(vm->stack));
        break;
    }
    case OP_SET_LOCAL: {
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        LOG("SET_LOCAL %s\n", key);
        env_set_local(vm->env, key, &array_top(vm->stack));
        break;
    }
    case OP_GET: {
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        const uint32_t hash =
                            vm->code.data[vm->ip+0] << 12 |
                            vm->code.data[vm->ip+1] << 8  |
                            vm->code.data[vm->ip+2] << 4  |
                            vm->code.data[vm->ip+3];
        vm->ip+=sizeof(hash);
        LOG("GET %s\n", key);
        array_push(vm->stack, (struct value){});
        struct value *val = env_get_hash(vm->env, key, hash);
        if(val == NULL) {
            FATAL("no key named %s!\n", key);
            return 0;
        } else
            value_copy(&array_top(vm->stack), val);
        break;
    }
    case OP_INC:
    case OP_DEC: {
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        if(op == OP_INC) LOG("INC %s\n", key);
        else LOG("DEC %s\n", key);
        struct value *val = env_get(vm->env, key);
        if(val->type == TYPE_INT) {
            if(op == OP_INC) val->as.integer++;
            else val->as.integer--;
        } else if(val->type == TYPE_FLOAT) {
            if(op == OP_INC) val->as.floatp++;
            else val->as.floatp--;
        } else {
            FATAL("must be int or float!\n");
            assert(0);
        }
        break;
    }
    case OP_DEF_FUNCTION:
    case OP_DEF_FUNCTION_PUSH: {
        // [opcode][key][end address]
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        const uint32_t pos = vm->code.data[vm->ip+0] << 12 |
                             vm->code.data[vm->ip+1] << 8  |
                             vm->code.data[vm->ip+2] << 4  |
                             vm->code.data[vm->ip+3];
        vm->ip += 4;
        uint32_t nargs = vm->code.data[vm->ip++];
        LOG("DEF_FUNCTION %s %d %d\n", key, pos, nargs);
        struct value val;
        value_function(&val, vm->ip, nargs);
        vm->ip = pos;
        if(op == OP_DEF_FUNCTION_PUSH)
            array_push(vm->stack, val);
        else
            env_set(vm->env, key, &val);
        break;
    }

    // flow control
    case OP_JMP: { // jmp [64-bit position]
        vm->ip++;
        const uint64_t pos = vm->code.data[vm->ip+0] << 12 |
                             vm->code.data[vm->ip+1] << 8  |
                             vm->code.data[vm->ip+2] << 4  |
                             vm->code.data[vm->ip+3];
        LOG("JMP %ld\n", pos);
        vm->ip = pos;
        break;
    }
    case OP_JCOND:
    case OP_JNCOND: { // jcond [64-bit position]
        vm->ip++;
        const uint64_t pos = vm->code.data[vm->ip+0] << 12 |
                             vm->code.data[vm->ip+1] << 8  |
                             vm->code.data[vm->ip+2] << 4  |
                             vm->code.data[vm->ip+3];
        struct value val = array_top(vm->stack);
        array_pop(vm->stack);
        if(op == OP_JCOND) {
            LOG("JCOND %ld\n", pos);
            if(value_is_true(&val)) vm->ip = pos;
            else vm->ip += 4;
        } else {
            LOG("JNCOND %ld\n", pos);
            if(!value_is_true(&val)) vm->ip = pos;
            else vm->ip += 4;
        }
        break;
    }
    case OP_CALL: {
        // argument: [arg2][arg1]
        vm->ip++;
        struct value val = array_top(vm->stack);
        int nargs = vm->code.data[vm->ip++];
        assert(vm->stack.length >= nargs);
        LOG("call %d\n", nargs);
        if(val.type == TYPE_NATIVE_FN) {
            array_pop(vm->stack);
            val.as.fn(vm, nargs);
        } else if(val.type == TYPE_FN || val.type == TYPE_DICT) {
            size_t fn_ip = 0;
            if(val.type == TYPE_DICT) {
                array_pop(vm->stack);
                struct value *ctor = dict_get(val.as.dict, "constructor");
                if(ctor == NULL) {
                    printf("expected dictionary to have constructor");
                    return 0;
                }
                assert(ctor->type == TYPE_FN || ctor->type == TYPE_NATIVE_FN);
                if(ctor->type == TYPE_NATIVE_FN) {
                    value_free(&val);
                    ctor->as.fn(vm, nargs);
                    return 1;
                }
                fn_ip = ctor->as.ifn.ip;
                if(nargs+1 != ctor->as.ifn.nargs) {
                    printf("constructor expects exactly %d arguments, got %d\n", ctor->as.ifn.nargs, nargs);
                    return 0;
                }
            } else {
                fn_ip = val.as.ifn.ip;
                array_pop(vm->stack);
                if(nargs != val.as.ifn.nargs) {
                    printf("function expects exactly %d arguments, got %d\n", val.as.ifn.nargs, nargs);
                    return 0;
                }
            }
            struct value args[nargs];
            for(int i = nargs-1; i >= 0; i--) {
                struct value val = array_top(vm->stack);
                array_pop(vm->stack);
                args[i] = val;
            }
            // caller
            struct value caller;
            value_function(&caller, vm->ip, 0);
            array_push(vm->stack, caller);
            // arguments
            if(val.type == TYPE_DICT) {
                if(vm->stack.length+nargs > vm->stack.capacity) {
                    vm->stack.capacity = vm->stack.length+nargs;
                    vm->stack.data = (struct value*)realloc(vm->stack.data,
                                                    sizeof(struct value)*vm->stack.capacity);
                }
                memcpy(vm->stack.data+vm->stack.length, args, sizeof(struct value)*nargs);

                struct value new_val;
                value_dict(&new_val);
                dict_set(new_val.as.dict, "prototype", &val);
                value_free(&val); // reference carried by dict
                array_push(vm->stack, new_val);
                nargs++;
            } else {
                for(int i = 0; i < nargs; i++)
                    array_push(vm->stack, args[i]);
            }
            // environment
            struct env *parent = vm->env;
            vm->env = malloc(sizeof(struct env));
            env_init(vm->env, parent);
            // jump
            vm->ip = fn_ip;
        } else {
            printf("is not a function\n");
            return 0;
        }
        break;
    }
    case OP_RET: {
        struct value retval = array_top(vm->stack);
        array_pop(vm->stack);

        struct value caller = array_top(vm->stack);
        array_pop(vm->stack);
        assert(caller.type == TYPE_FN);

        LOG("RET\n");
        vm->ip = caller.as.ifn.ip;
        array_push(vm->stack, retval);

        assert(vm->env->parent != NULL);
        struct env *parent = vm->env->parent;
        env_free(vm->env);
        free(vm->env);
        vm->env = parent;
        break;
    }

    // scoped
    case OP_ENV_INHERIT: {
        vm->ip++;
        LOG("ENV_INHERIT\n");
        struct env *parent = vm->env;
        vm->env = malloc(sizeof(struct env));
        env_init(vm->env, parent);
        break;
    }
    case OP_ENV_POP: {
        vm->ip++;
        LOG("ENV_POP\n");
        assert(vm->env->parent != NULL);
        struct env *parent = vm->env->parent;
        env_free(vm->env);
        free(vm->env);
        vm->env = parent;
        break;
    }

    // dictionaries
    case OP_DICT_NEW: {
        vm->ip++;
        LOG("DICT_NEW\n");
        array_push(vm->stack, (struct value){});
        value_dict(&array_top(vm->stack));
        break;
    }
    case OP_MEMBER_GET:
    case OP_MEMBER_GET_NO_POP: {
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        LOG("MEMBER_GET %s\n", key);

        struct value val = array_top(vm->stack);
        struct dict *dict = NULL;
        if(val.type == TYPE_STR) {
            dict = vm->dstr;
        } else if(val.type == TYPE_INT) {
            dict = vm->dint;
        } else if(val.type == TYPE_FLOAT) {
            dict = vm->dfloat;
        } else {
            assert(val.type == TYPE_DICT);
            dict = val.as.dict;
            if(op == OP_MEMBER_GET) array_pop(vm->stack);
        }

        array_push(vm->stack, (struct value){});
        struct value *result = dict_get(dict, key);
        if(result != NULL)
            value_copy(&array_top(vm->stack), result);

        if(op == OP_MEMBER_GET) value_free(&val);
        break;
    }
    case OP_MEMBER_SET: {
        // stack: [value][dict]
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        LOG("MEMBER_SET %s\n", key);
        struct value dval = array_top(vm->stack);
        assert(dval.type == TYPE_DICT);
        array_pop(vm->stack);

        struct value val = array_top(vm->stack);
        dict_set(dval.as.dict, key, &val);
        value_free(&dval);
        break;
    }
    case OP_DICT_LOAD: {
        // stack: [nil][value][key]
        vm->ip++;
        struct value dval;
        value_dict(&dval);

        struct value key = {0};
        while((key = array_top(vm->stack)).type != TYPE_NIL) {
            assert(key.type == TYPE_STR);
            array_pop(vm->stack); // pop val
            struct value val = array_top(vm->stack);
            array_pop(vm->stack);
            dict_set(dval.as.dict, key.as.str, &val);
            // pop key
            value_free(&val);
            value_free(&key);
        }
        array_pop(vm->stack); // pop nil
        array_push(vm->stack, dval);
        break;
    }
    // array
    case OP_INDEX_GET: {
        vm->ip++;
        struct value index;
        value_copy(&index, &array_top(vm->stack));
        value_free(&array_top(vm->stack));
        array_pop(vm->stack);

        struct value dval = array_top(vm->stack);
        array_pop(vm->stack);

        if(dval.type == TYPE_ARRAY) {
            assert(index.type == TYPE_INT);
            const int64_t i = index.as.integer;
            assert(i >= 0 && i < dval.as.array->data.length);
            printf("%d\n", dval.as.array->data.length);
            array_push(vm->stack, (struct value){});
            value_copy(&array_top(vm->stack), &dval.as.array->data.data[i]);
        } else if(dval.type == TYPE_DICT) {
            assert(index.type == TYPE_STR);
            array_push(vm->stack, (struct value){});
            struct value *val = dict_get(dval.as.dict, index.as.str);
            if(val) value_copy(&array_top(vm->stack), val);
        } else {
            printf("expected dictionary or array\n");
            return 0;
        }
        value_free(&dval);
        break;
    }
    case OP_ARRAY_LOAD: {
        vm->ip++;

        struct value val = array_top(vm->stack);
        int length = val.as.integer;
        array_pop(vm->stack);
        assert(val.type == TYPE_INT);
        LOG("ARRAY_LOAD %d\n", length);

        struct value aval;
        if(length == 0) {
            value_array(&aval);
        } else {
            value_array_n(&aval, length);
            aval.as.array->data.length = length;
            while(length--) {
                struct value val = array_top(vm->stack);
                value_copy(&aval.as.array->data.data[length], &val);
                array_pop(vm->stack);
                value_free(&val);
            }
        }
        array_push(vm->stack, aval);
        break;
    }

    // variable modification
    case OP_ADDS: {
        vm->ip++;
        char *key = (char *)&vm->code.data[vm->ip]; // must be null terminated
        vm->ip += strlen(key)+1;
        LOG("ADDS %s\n", key);
        struct value right = array_top(vm->stack);
        struct value *val = env_get(vm->env, key);
        struct value result;
        value_add(&result, val, &right);
        value_free(val);
        value_copy(val, &result);
        value_free(&result);
        break;
    }

    // end
    default: {
        FATAL("undefined opcode: %d\n", op);
        assert(0);
    }
    }

#ifndef NOLOG
    vm_print_stack(vm);
#endif
    return 1;
}

void vm_execute(struct vm *vm) {
    while(vm_step(vm));
}

void vm_print_stack(const struct vm *vm) {
    printf("[");
    for(size_t i = 0; i < vm->stack.length; i++) {
        value_print(&vm->stack.data[i]);
        printf(" ");
    }
    printf("]\n");
}

// push bits
void vm_code_push16(struct vm *vm, uint16_t n) {
    array_push(vm->code, (n >> 4) & 0xff);
    array_push(vm->code, (n >> 0) & 0xff);
}

void vm_code_push32(struct vm *vm, uint32_t n) {
    array_push(vm->code, (n >> 12) & 0xff);
    array_push(vm->code, (n >> 8)  & 0xff);
    array_push(vm->code, (n >> 4)  & 0xff);
    array_push(vm->code, (n >> 0)  & 0xff);
}

void vm_code_push64(struct vm *vm, uint64_t n) {
    array_push(vm->code, (n >> 28) & 0xff);
    array_push(vm->code, (n >> 24) & 0xff);
    array_push(vm->code, (n >> 20) & 0xff);
    array_push(vm->code, (n >> 16) & 0xff);
    array_push(vm->code, (n >> 12) & 0xff);
    array_push(vm->code, (n >> 8)  & 0xff);
    array_push(vm->code, (n >> 4)  & 0xff);
    array_push(vm->code, (n >> 0)  & 0xff);
}

void vm_code_pushstr(struct vm *vm, const char *s) {
    for(int i = 0; s[i]; i++)
        array_push(vm->code, s[i]);
    array_push(vm->code, 0);
}

void vm_code_pushf32(struct vm *vm, float f) {
    union {
        float f;
        uint8_t u[4];
    } u;
    u.f = f;
    array_push(vm->code, u.u[0]);
    array_push(vm->code, u.u[1]);
    array_push(vm->code, u.u[2]);
    array_push(vm->code, u.u[3]);
}
void vm_code_pushf64(struct vm *vm, double d) {
    union {
        double d;
        uint8_t u[8];
    } u;
    u.d = d;
    array_push(vm->code, u.u[0]);
    array_push(vm->code, u.u[1]);
    array_push(vm->code, u.u[2]);
    array_push(vm->code, u.u[3]);
    array_push(vm->code, u.u[4]);
    array_push(vm->code, u.u[5]);
    array_push(vm->code, u.u[6]);
    array_push(vm->code, u.u[7]);
}
