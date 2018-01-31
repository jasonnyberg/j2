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
    VMRES_WIP,
    VMRES_STACK,
    VMRES_EXC,
    VMRES_CLL_COUNT,
    VMRES_STATE=VMRES_CLL_COUNT,
    VMRES_SKIP,
    VMRES_COUNT
    // REFS? EXCEPTIONS?
};

enum {
    VM_ERROR    = 0x01,
    VM_BYPASS   = 0x02,
    VM_THROWING = 0x04,
    VM_SKIPPING = 0x08,
    VM_COMPLETE = 0x10,
};

extern int vm_boot();

#endif // VM_H
