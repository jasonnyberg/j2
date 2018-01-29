/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMPILE_H
#define COMPILE_H

#include "listree.h"

typedef struct {
    unsigned char op;
    unsigned int len; // extended
    LTV_FLAGS flags;  // extended
    char *data;       // extended
} VM_CMD; // exploded bytecode template

typedef int (*EMITTER)(VM_CMD *cmd);
typedef int (*COMPILER)(EMITTER emit,void *data,int len);

enum {
    FORMAT_asm,
    FORMAT_edict,
    FORMAT_xml,
    FORMAT_json,
    FORMAT_yaml,
    FORMAT_swagger,
    FORMAT_lisp,
    FORMAT_massoc,
    FORMAT_MAX
} VM_COMPILERS;

enum {
    VMOP_NOP=0,
    VMOP_LIT,
    VMOP_REF,
    VMOP_EXTENDED=0x10, // not an op; any ops less than this are "extended"

    VMOP_BUILTIN,
    VMOP_REF_MAKE,
    VMOP_REF_KILL,
    VMOP_REF_INS,
    VMOP_REF_RES,
    VMOP_REF_ERES, // HRES but not skipped while throwing (for catch)
    VMOP_REF_HRES, // ERES but skipped while throwing
    VMOP_REF_ITER,
    VMOP_ASSIGN,
    VMOP_REMOVE,
    VMOP_APPEND,
    VMOP_COMPARE,
    VMOP_DEREF,

    VMOP_MMAP_KEEP, // make map keep
    VMOP_MMAP_POP,  // make map pop
    VMOP_MAP_KEEP,  // do map keep
    VMOP_MAP_POP,   // do map pop

    VMOP_BYTECODE,

    VMOP_THROW,
    VMOP_CATCH,

    VMOP_CONCAT,
    VMOP_LISTCAT,

    VMOP_PUSH_SUB,
    VMOP_EVAL_SUB,
    VMOP_POP_SUB,

    VMOP_NULL_ITEM,
    VMOP_NULL_LIST,

    VMOP_ENFRAME,
    VMOP_DEFRAME,

    VMOP_TOS,

    VMOP_RDLOCK,
    VMOP_WRLOCK,
    VMOP_UNLOCK,

    VMOP_YIELD,

    VMOP_SPUSH,
    VMOP_SPOP,
    VMOP_SPEEK,

    VMOP_PUSH,
    VMOP_POP,
    VMOP_PEEK,
    VMOP_DUP,  // dup TOS(res)
    VMOP_DROP, // drop TOS(res)

    VMOP_EDICT,
    VMOP_XML,
    VMOP_JSON,
    VMOP_YAML,
    VMOP_SWAGGER,
    VMOP_LISP,
    VMOP_MASSOC,

    // 0xff=VMRES_*
} VM_BYTECODES;

extern char *formats[];
extern COMPILER compilers[];

extern LTV *compile(COMPILER compiler,void *data,int len);
extern LTV *compile_ltv(COMPILER compiler,LTV *ltv);

extern int jit_asm(EMITTER emit,void *data,int len);
extern int jit_edict(EMITTER emit,void *data,int len);
extern int jit_xml(EMITTER emit,void *data,int len);
extern int jit_json(EMITTER emit,void *data,int len);
extern int jit_yaml(EMITTER emit,void *data,int len);
extern int jit_swagger(EMITTER emit,void *data,int len);
extern int jit_lisp(EMITTER emit,void *data,int len);
extern int jit_massoc(EMITTER emit,void *data,int len); // mathematica association

#endif // COMPILE_H
