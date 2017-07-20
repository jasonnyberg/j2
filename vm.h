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


#ifndef VM_H
#define VM_H

#include "listree.h"

// ideas:
// cll/listree traversal bytecodes
// generic a->b djykstra
// use these to implement coercion hierarchy/polymporphism
// LTV *coerce(LTV *from)

enum
{
    VMOP_NULL=0, // null terminator

    VMOP_RDLOCK,
    VMOP_WRLOCK,
    VMOP_UNLOCK,

    VMOP_LTV,
    VMOP_DUP,

    VMOP_REF_CREATE,
    VMOP_REF_INSERT, // force create on top context
    VMOP_REF_ASSIGN,
    VMOP_REF_REMOVE,
    VMOP_REF_RESOLVE,
    VMOP_REF_ITER_KEEP,
    VMOP_REF_ITER_POP,
    VMOP_REF_RESET,
    VMOP_REF_DELETE,

    VMOP_EVAL,

    VMOP_SCOPE_OPEN,
    VMOP_SCOPE_CLOSE,

    VMOP_YIELD,

    VMOP_PRINT_STACK,
    VMOP_GRAPH_STACK,
    VMOP_PRINT_REF,
    VMOP_GRAPH_REF,
};

enum
{
    VMRES_CODE,
    VMRES_IP,
    VMRES_STACK,
    VMRES_DICT,
    VMRES_REF,
    VMRES_EXC,
    VMRES_COUNT
};

typedef struct {
    LTV ltv;
    LTV *tos[VMRES_COUNT]; // top of each res stack
    CLL ros[VMRES_COUNT]; // stack of code/stack/dict/refs
} VM_ENV;

typedef struct {
    unsigned length;
    unsigned flags;
    char data[0];
} VM_BC_LTV; // packed extended bytecode

typedef struct {
    char op;
    int len;
    LTV_FLAGS flags;
    char *data;
} VM_CMD; // exploded bytecode template

extern int vm_init(int argc,char *argv[]);
extern int vm_eval(VM_ENV *env);

#endif // VM_H
