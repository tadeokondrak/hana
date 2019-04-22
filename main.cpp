#include <iostream>
#include <memory>
#include <cmath>
#include <getopt.h>
#include <limits.h>
#include "src/scriptparser.h"
#include "hanayo/native/hanayo.h"
#ifdef LREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#ifdef INCLUDE_BYTECODE
#include "incbin.h"
INCBIN(InitBytecode, "build/init.bin");
#endif


static void help(char *program) {
printf(
"usage: %s [-c cmd | file | -]\n\
options: \n\
 -c cmd : execute program passed in as string\n\
 -d/--dump-vmcode: dumps vm bytecode to stdout\n\
                   (only works in interpreter mode)\n\
 -b/--bytecode: runs file as bytecode\n\
 -a/--print-ast: prints ast\n\
 -n/--no-run: just parse the file, don't run\n\
\n\
",
    program
);
}

static void version() {
printf("\
original interpreter for the hana programming language (alpha).\n\
\n\
This program is free software: you can redistribute it\n\
and/or modify it under the terms of the GNU General Public License\n\
as published by the Free Software Foundation, either version 3 of\n\
the License, or (at your option) any later version.\n\
\n\
");
}

int main(int argc, char **argv) {

    int last_optiond = 1;
    int command_optiond = -1;
    bool opt_dump_vmcode = false,
         opt_print_ast = false,
         opt_no_run = false,
         opt_bytecode = false;
    while(1) {
        int c = 0;
        static struct option options[] = {
            { "help",        no_argument,       NULL, 0   },
            { "version",     no_argument,       NULL, 0   },
            { "dump-vmcode", no_argument,       NULL, 0   },
            { "print-ast",   no_argument,       NULL, 0   },
            { "no-run",      no_argument,       NULL, 0   },
            { "command",     required_argument, NULL, 'c' },
            { "bytecode",    no_argument,       NULL, 'b' },
            { 0,             0, NULL, 0 },
        };
        int option_index = 0;
        c = getopt_long(argc, argv, "hvdanc:0:b", options, &option_index);
        if(c == -1) break;
        switch(c) {
        case 'h':
            help(argv[0]);
            return 0;
        case 'v':
            version();
            return 0;
        case 'd':
            opt_dump_vmcode = true;
            break;
        case 'a':
            opt_print_ast = true;
            break;
        case 'n':
            opt_no_run = true;
            break;
        case 'c':
            command_optiond = optind;
            break;
        case 'b':
            opt_bytecode = true;
        case '0':
            break;
        default:
            help(argv[0]);
            return 1;
        }
        last_optiond = optind;
    }

    Hana::Compiler compiler;

    // dump
    if(opt_dump_vmcode) {
        if((argc-last_optiond) != 1) {
            printf("dump vmcode option only works with files\n");
            return 1;
        }
        struct vm m; vm_init(&m);
        Hana::ScriptParser p;
        p.loadf(argv[last_optiond]);
        auto ast = std::unique_ptr<Hana::AST::AST>(p.parse());
        if(ast == nullptr) return 1;
        ast->emit(&m, &compiler);
        fwrite(m.code.data, 1, m.code.length, stdout);
        vm_free(&m);
        return 0;
    }


    // virtual machine
    struct vm m; vm_init(&m);
#ifdef INCLUDE_BYTECODE
    free(m.code.data);
    m.code.data = (uint8_t*)malloc(gInitBytecodeSize);
    memcpy(m.code.data, gInitBytecodeData, gInitBytecodeSize);
    m.code.length = gInitBytecodeSize;
    m.code.capacity = gInitBytecodeSize;
#endif
    hanayo::_init(&m);

    // command
    if(command_optiond != -1) {
        Hana::ScriptParser p;
        std::string s(argv[command_optiond-1]);
        p.loads(s);
        auto ast = std::unique_ptr<Hana::AST::AST>(p.parse());
        if(ast == nullptr) return 1;
        if(opt_print_ast) ast->print();
        ast->emit(&m, &compiler);
        array_push(m.code, OP_HALT);
        vm_execute(&m);
        goto cleanup;
    }
    // repl
    else if((argc-last_optiond) == 0) {
        #ifdef LREADLINE
        rl_bind_key('\t', rl_insert);
        #endif
        while(1) {
            #ifdef LREADLINE
            int nread = 0;
            std::string s;
            while(1) {
                char *buf = readline(nread == 0 ? ">> " : "");
                if(buf == nullptr) {
                    goto cleanup;
                } else if(buf[strlen(buf)-1] == '\\') {
                    add_history(buf);
                    buf[strlen(buf)-1] = 0;
                    s += buf;
                    s += '\n';
                    nread++;
                    free(buf);
                } else {
                    add_history(buf);
                    s += buf;
                    s += '\n';
                    free(buf);
                    break;
                }
            }
            #else
            std::cout << ">> ";
            std::string s, line;
            while(1) {
                std::getline(std::cin, line);
                if(std::cin.eof()) goto cleanup;
                if(line[line.size()-1] == '\\') {
                    line.resize(line.size()-1);
                    s += line+'\n';
                } else {
                    s += line+'\n';
                    break;
                }
            }
            #endif
            if(s.empty()) continue;
            Hana::ScriptParser p;
            p.loads(s);
            auto ast = std::unique_ptr<Hana::AST::AST>(p.parse());
            if(ast == nullptr) continue;
            if(opt_print_ast) ast->print();
            ast->emit(&m, &compiler);
            array_push(m.code, OP_HALT);
            vm_execute(&m);
            m.ip = m.code.length;
        }
    }
    // file
    else if(opt_bytecode) {
        std::ifstream file(argv[last_optiond], std::ios::binary | std::ios::ate);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        vm_code_reserve(&m, size);
        file.read((char*)m.code.data, size);
        vm_execute(&m);
        goto cleanup;
    } else {
        Hana::ScriptParser p;
        p.loadf(argv[last_optiond]);
        auto ast = std::unique_ptr<Hana::AST::AST>(p.parse());
        if(ast == nullptr) return 1;

        // succesfully parsed
        if(opt_print_ast) ast->print();
        ast->emit(&m, &compiler);
        if(opt_no_run) goto cleanup;

        // set up __file__
        struct value val;
        char actualpath[PATH_MAX+1];
        char *ptr = ::realpath(argv[last_optiond], actualpath);
        if(ptr != nullptr) {
            value_str(&val, ptr);
            hmap_set(&m.globalenv, "__file__", &val);
            value_free(&val);
        }

        // run
        array_push(m.code, OP_HALT);
        vm_execute(&m);
        goto cleanup;
    }

cleanup:
    vm_free(&m);
    return 0;
}
