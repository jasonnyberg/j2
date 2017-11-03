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


/*
  Parity Operations

  while (parser generates bytecode)
      [
         case {in_throw,is_catch} of
             {false,false} evaluate bytecode;
             {false,true} skip to pop_context;
             {true,false} skip until catch;
             {true,true} stop skipping, restart evaluation
        || bytecode <- bytecodes]

  eval_push
  edict_resolve // walk up stack of lexical scopes looking for reference; inserts always resolve at most-local scope

  deref
  assign
  remove
  scope_open
  scope_close
  function_close
  concatenate
  map
  eval
  compare
  throw
  catch

  ref_eval // solo ref, i.e. not part of an atom; must have lambda attached
  atom_eval // ops and/or ref
  lit_eval // push (if not skipping)
  expr_eval
  ffi_eval
  file_eval
  "tok_eval" (tokenizer would spawn an ffi subtok rather than parse a cvar expression)
  "thread_eval"

  edict_init
  edict_destroy
  edict


  special
  * readfrom (push file_tok(filename))
  * import (curate dwarf)
  * preview (dwarf->TOC)
  * cvar (pop_type()->ref_create_cvar()->push())
  * ffi (type->ref_ffi_prep)
  * dump (TOS or whole stack)
  * dup (TOS)
  * ro (TOS->set read_only)

*/

enum {
    VMOP_NULL=0, // null terminator

    VMOP_LIT,
    VMOP_REF,
    VMOP_BUILTIN,
    VMOP_EVAL,

    VMOP_MAKEREF,
    VMOP_DEREF,
    VMOP_ASSIGN,
    VMOP_REMOVE,
    VMOP_THROW,
    VMOP_CATCH,
    VMOP_MAP,
    VMOP_APPEND,
    VMOP_COMPARE,

    VMOP_RDLOCK,
    VMOP_WRLOCK,
    VMOP_UNLOCK,

    VMOP_YIELD,

    VMOP_DUMP_ENV,

    VMOP_SPUSH,
    VMOP_SPOP,
    VMOP_SPEEK,
    VMOP_SDUP,
    VMOP_SDROP,

    VMOP_PUSH,
    VMOP_POP,
    VMOP_PEEK,
    VMOP_DUP,  // dup TOS(res)
    VMOP_DROP, // drop TOS(res)

    VMOP_RES_0=0xf8,
    VMOP_RES_1=0xf9,
    VMOP_RES_2=0xfa,
    VMOP_RES_3=0xfb,
    VMOP_RES_4=0xfc,
    VMOP_RES_5=0xfd,
    VMOP_RES_6=0xfe,
    VMOP_RES_7=0xff,
};

enum {
    VMRES_STACK, // values can be plain lit, cvar, or ref
    VMRES_CODE,
    VMRES_DICT,
    VMRES_REFS,
    VMRES_IP,
    VMRES_UNUSED1,
    VMRES_UNUSED2,
    VMRES_WIP,
    VMRES_COUNT
    // REFS? EXCEPTIONS?
};

enum {
    ENV_RUNNING=0,
    ENV_EXHAUSTED,
    ENV_EXCEPTION,
    ENV_BROKEN
};

typedef struct {
    LTV ltv;
    unsigned state;
    LTV *tos[VMRES_COUNT]; // "resource" top-of-stack
    CLL ros[VMRES_COUNT];  // "resource" stack (code/stack/dict/refs)
    //LTV *reg[VMRES_COUNT]; // "registers"
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
