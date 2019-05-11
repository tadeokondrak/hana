use std::ptr::null_mut;
use std::ffi::CString;
extern crate libc;

use super::carray::CArray;
use super::chmap::CHashMap;
use super::record::Record;
use super::function::Function;
use super::cnativeval::NativeValue;
use super::env::Env;
use super::gc::*;
pub use super::value::Value;

//
#[repr(u8)]
#[allow(non_camel_case_types)]
pub enum VmOpcode {
    OP_HALT,
    // stack manip
    OP_PUSH8, OP_PUSH16, OP_PUSH32, OP_PUSH64,
    OP_PUSH_NIL, OP_PUSHSTR, OP_PUSHF32, OP_PUSHF64,
    OP_POP,
    // arith
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    // logic
    OP_AND, OP_OR,
    // unary
    OP_NEGATE, OP_NOT,
    // comparison
    OP_LT, OP_LEQ, OP_GT, OP_GEQ,
    OP_EQ, OP_NEQ,
    // variables
    OP_ENV_NEW,
    OP_SET_LOCAL, OP_SET_LOCAL_FUNCTION_DEF, OP_GET_LOCAL,
    OP_SET_LOCAL_UP, OP_GET_LOCAL_UP,
    OP_SET_GLOBAL, OP_GET_GLOBAL,
    OP_DEF_FUNCTION_PUSH,
    // flow control
    OP_JMP, OP_JCOND, OP_JNCOND, OP_CALL, OP_RET,
    // dictionary
    OP_DICT_NEW, OP_MEMBER_GET, OP_MEMBER_GET_NO_POP,
    OP_MEMBER_SET, OP_DICT_LOAD, OP_ARRAY_LOAD,
    OP_INDEX_GET, OP_INDEX_SET,
    // exceptions
    OP_TRY, OP_RAISE, OP_EXFRAME_RET,
    // tail calls
    OP_RETCALL
}

#[repr(C)]
pub struct Vm {
    pub ip        : u32, // current instruction pointer
    pub localenv  : *mut Env,
    // represents a linked list of call frames
    pub globalenv : *mut CHashMap,
    // global environment, all unscoped variables/variables
    // starting with '$' should be stored here
    eframe        : *mut u8, // exception frame (TODO)
    pub code      : CArray<VmOpcode>, // where all the code is
    pub stack     : CArray<NativeValue>, // stack

    // prototype types for primitive values
    pub dstr      : *mut Record,
    pub dint      : *mut Record,
    pub dfloat    : *mut Record,
    pub darray    : *mut Record,

    pub error     : bool
    // whether the interpreter raised an unhandled error
}

#[link(name="hana", kind="static")]
#[allow(improper_ctypes)]
extern "C" {
    fn vm_init(vm: *mut Vm);
    fn vm_free(vm: *mut Vm);
    fn vm_execute(vm: *mut Vm);
    fn vm_print_stack(vm: *const Vm);
    fn vm_call(vm: *mut Vm, fun: NativeValue, args: CArray<NativeValue>)
        -> NativeValue;

    fn vm_code_push8  (vm: *mut Vm, n : u8);
    fn vm_code_push16 (vm: *mut Vm, n : u16);
    fn vm_code_push32 (vm: *mut Vm, n : u32);
    fn vm_code_push64 (vm: *mut Vm, n : u64);
    fn vm_code_pushstr(vm: *mut Vm, s : *const libc::c_char);
    fn vm_code_pushf32(vm: *mut Vm, n : f32);
    fn vm_code_pushf64(vm: *mut Vm, n : f64);
    fn vm_code_fill(vm: *mut Vm, pos : u32, len : u32);
    fn vm_code_fill16(vm: *mut Vm, pos : u32, len : u16);
}

impl Vm {
    pub fn new() -> Vm {
        let mut vm = Vm{
            ip: 0,
            localenv: null_mut(),
            globalenv: null_mut(),
            eframe: null_mut(),
            code: CArray::new_nil(),
            stack: CArray::new_nil(),
            dstr: null_mut(),
            dint: null_mut(),
            dfloat: null_mut(),
            darray: null_mut(),
            error: false,
        };
        unsafe { vm_init(&mut vm); }
        vm
    }

    pub fn print_stack(&self) {
        unsafe { vm_print_stack(self); }
    }

    pub fn execute(&mut self) {
        unsafe { vm_execute(self); }
    }

    // pushes
    pub fn cpush8(&mut self, n : u8) { unsafe { vm_code_push8(self, n); } }
    pub fn cpush16(&mut self, n : u16) { unsafe { vm_code_push16(self, n); } }
    pub fn cpush32(&mut self, n : u32) { unsafe { vm_code_push32(self, n); } }
    pub fn cpush64(&mut self, n : u64) { unsafe { vm_code_push64(self, n); } }
    pub fn cpushf32(&mut self, n : f32) { unsafe { vm_code_pushf32(self, n); } }
    pub fn cpushf64(&mut self, n : f64) { unsafe { vm_code_pushf64(self, n); } }
    pub fn cpushs<T : Into<Vec<u8>>>(&mut self, s : T) {
        let cstr = CString::new(s).expect("can't turn to cstring");
        unsafe { vm_code_pushstr(self, cstr.as_ptr()); }
    }

    pub fn cfill_label(&mut self, pos: usize, label: usize) {
        unsafe{ vm_code_fill(self, pos as u32, label as u32); }
    }
    pub fn cfill_label16(&mut self, pos: usize, label: u16) {
        unsafe{ vm_code_fill16(self, pos as u32, label); }
    }

    // globals
    pub fn global(&mut self) -> &mut CHashMap {
        if self.globalenv.is_null() { panic!("accessing nil ptr"); }
        unsafe{ &mut *self.globalenv }
    }

    // gc
    pub fn mark(&mut self) {
        // globalenv
        let globalenv = self.global();
        for (_, val) in globalenv.iter() {
            val.mark();
        }
        // stack
        let stack = &self.stack;
        for val in stack.iter() {
            val.mark();
        }
        // call stack
        unsafe {
            let mut env = self.localenv;
            while !env.is_null() {
                for i in 0..(*env).nslots {
                    let val = (*env).get(i);
                    val.mark();
                }
                env = (*env).parent;
            }
        }
    }

    // call stack
    pub fn leave_env(&mut self) {
        // we don't check for env leaving
        // this must be non-null
        unsafe {
            let parent = (*self.localenv).parent;
            self.ip = (*self.localenv).retip;
            std::boxed::Box::from_raw(self.localenv);
            self.localenv = parent;
        }
    }

    // functions
    pub fn call(&mut self, fun: NativeValue, args: CArray<NativeValue>) -> NativeValue {
        unsafe{ vm_call(self, fun, args) }
    }
}

impl std::ops::Drop for Vm {
    fn drop(&mut self) {
        unsafe { vm_free(self); }
    }
}
