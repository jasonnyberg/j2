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
    VMRES_DICT,
    VMRES_CODE,
    VMRES_IP,
    VMRES_WIP,
    VMRES_STACK,
    VMRES_EXC,
    VMRES_COUNT
    // REFS? EXCEPTIONS?
};

enum {
    VMOP_NOP=0,
    VMOP_LIT,
    VMOP_REF,

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
    VMOP_MAP,

    VMOP_THROW,
    VMOP_CATCH,

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

    VMOP_RES_DICT  = 0xff-VMRES_DICT,
    VMOP_RES_CODE  = 0xff-VMRES_CODE,
    VMOP_RES_IP    = 0xff-VMRES_IP,
    VMOP_RES_WIP   = 0xff-VMRES_WIP,
    VMOP_RES_STACK = 0xff-VMRES_STACK,
    VMOP_RES_EXC   = 0xff-VMRES_EXC,
};

enum {
    VM_ERROR    = 0x01,
    VM_BYPASS   = 0x02,
    VM_THROWING = 0x04,
    VM_SKIPPING = 0x10,
};

typedef struct {
    LTV lnk;
    unsigned state;
    unsigned skipdepth;
    CLL res[VMRES_COUNT];  // "resource" stack (code/stack/dict/refs)
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
