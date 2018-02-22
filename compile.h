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

#include <stdio.h>
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
    VMOP_RESET,
    VMOP_EXT,
    VMOP_THROW,
    VMOP_CATCH,
    VMOP_PUSHEXT,
    VMOP_EVAL,
    VMOP_REF,
    VMOP_DEREF,
    VMOP_ASSIGN,
    VMOP_REMOVE,
    VMOP_CTX_PUSH,
    VMOP_CTX_POP,
    VMOP_FUN_PUSH,
    VMOP_FUN_EVAL,
} VM_OPCODES;

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

extern char *formats[];
extern COMPILER compilers[];

extern LTV *compile(COMPILER compiler,void *data,int len);
extern LTV *compile_ltv(COMPILER compiler,LTV *ltv);
extern void disassemble(FILE *ofile,LTV *ltv);

extern int jit_asm(EMITTER emit,void *data,int len);
extern int jit_edict(EMITTER emit,void *data,int len);
extern int jit_xml(EMITTER emit,void *data,int len);
extern int jit_json(EMITTER emit,void *data,int len);
extern int jit_yaml(EMITTER emit,void *data,int len);
extern int jit_swagger(EMITTER emit,void *data,int len);
extern int jit_lisp(EMITTER emit,void *data,int len);
extern int jit_massoc(EMITTER emit,void *data,int len); // mathematica association

#endif // COMPILE_H
