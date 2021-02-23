/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 * Copyright (C) 2018 Jason Nyberg <jasonnyberg@gmail.com> (dual-licensed)
 * (C) Copyright 2019 Hewlett Packard Enterprise Development LP.
 *
 * This file is part of j2.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either
 *
 *   * the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version
 *
 * or
 *
 *   * the GNU General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version
 *
 * or both in parallel, as here.
 *
 * j2 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and
 * the GNU Lesser General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#define _C99
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include <dlfcn.h>
#include <arpa/inet.h>

#include "util.h"
#include "cll.h"
#include "listree.h"
#include "reflect.h"
#include "vm.h"
#include "compile.h"
#include "extensions.h"
#include "trace.h"

pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

enum {
      VMRES_DICT,  // stack of dictionary frames
      VMRES_STACK, // stack of data stacks
      VMRES_FUNC,  // stack of pending f() calls
      VMRES_EXCP,  // exception for bypass/throw
      VMRES_CODE,
      VMRES_COUNT,
} VM_LISTRES;

static char *res_name[] = { "VMRES_DICT","VMRES_STACK","VMRES_FUNC","VMRES_EXCP","VMRES_CODE" };

enum {
      VM_YIELD    = 0x01,
      VM_BYPASS   = 0x02,
      VM_THROWING = 0x04,
      VM_COMPLETE = 0x08,
      VM_ERROR    = 0x10,
} VM_STATE;

typedef struct {
    LTV ltv[VMRES_COUNT];
    LTV *code_ltv;
    LTVR *code_ltvr;
    LTV *ext;
    char *ext_data;
    unsigned ext_length;
    unsigned ext_flags;
    unsigned state;
    unsigned skipdepth;
} VM_ENV;

__thread LTV *vm_env_stack=NULL; // every thread can have a stack of vm_environments (supports interpreter->C->interpreter->C...
__thread VM_ENV *vm_env=NULL; // every thread can have an active vm environment

static LTV *vm_ltv_container=NULL; // holds LTVs we don't want garbage-collected
static LTV *fun_pop_bc=NULL;

__attribute__((constructor))
static void init(void)
{
    VM_CMD fun_pop_asm[] = {{VMOP_FUN_POP}};
    vm_ltv_container=LTV_NULL_LIST;
    fun_pop_bc=compile(jit_asm,fun_pop_asm,1);
    LTV_put(LTV_list(vm_ltv_container),fun_pop_bc,HEAD,NULL);
}


//////////////////////////////////////////////////
void debug(const char *fromwhere) { return; }
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Basic Utillties

#define ENV_LIST(res) LTV_list(&vm_env->ltv[res])

static LTV *vm_enq(int res,LTV *tos) { return LTV_put(ENV_LIST(res),tos,HEAD,NULL); }
static LTV *vm_deq(int res,int pop)  { return LTV_get(ENV_LIST(res),pop,HEAD,NULL,NULL); }

LTV *vm_stack_enq(LTV *ltv) { return LTV_enq(LTV_list(vm_deq(VMRES_STACK,KEEP)),ltv,HEAD); }
LTV *vm_stack_deq(int pop) {
    void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
    LTV *rval=CLL_map(ENV_LIST(VMRES_STACK),FWD,op);
    return rval;
}

static void vm_reset_ext() {
    vm_env->ext_length=vm_env->ext_flags=0;
    vm_env->ext_data=NULL;
    if (vm_env->ext)
        LTV_release(vm_env->ext);
    vm_env->ext=NULL;
}

void vm_throw(LTV *ltv) {
    vm_env->state|=VM_THROWING;
    vm_enq(VMRES_EXCP,(ltv));
    vm_reset_ext();
}

//////////////////////////////////////////////////
// Specialized Utillties

static LTV *vm_use_ext() {
    if (vm_env->ext_data && !vm_env->ext)
        vm_env->ext=LTV_init(NEW(LTV),vm_env->ext_data,vm_env->ext_length,vm_env->ext_flags);
    return vm_env->ext;
}

static void vm_listcat(int res) { // merge tos into head of nos
    TSTART(vm_env->state,"");
    LTV *tos,*nos;
    THROW(!(tos=vm_deq(res,POP)),LTV_NULL);
    THROW(!(nos=vm_deq(res,KEEP)),LTV_NULL);
    THROW(!(tos->flags&LT_LIST && nos->flags&LT_LIST),LTV_NULL);
    CLL_MERGE(LTV_list(nos),LTV_list(tos),HEAD);
    LTV_release(tos);
 done:
    TFINISH(vm_env->state,"");
    return;
}

static void vm_code_push(LTV *ltv) {
    TSTART(vm_env->state,"");
    THROW(!ltv,LTV_NULL);
    LTV *opcode_ltv=LTV_init(NEW(LTV),ltv->data,ltv->len,LT_BIN|LT_LIST|LT_BC);
    THROW(!opcode_ltv,LTV_NULL);
    DEBUG(fprintf(ERRFILE,CODE_RED "  vm_code_push %x" CODE_RESET "\n",opcode_ltv->data));
    THROW(!LTV_enq(LTV_list(opcode_ltv),ltv,HEAD),LTV_NULL); // encaps code ltv within tracking ltv
    THROW(!vm_enq(VMRES_CODE,opcode_ltv),LTV_NULL);
 done:
    TFINISH(vm_env->state,"");
    return;
}

static void vm_code_peek() {
    TSTART(vm_env->state,"");
    vm_env->code_ltvr=NULL;
    THROW(!(vm_env->code_ltv=LTV_get(ENV_LIST(VMRES_CODE),KEEP,HEAD,NULL,&vm_env->code_ltvr)),LTV_NULL);
    DEBUG(fprintf(ERRFILE,CODE_RED "  vm_code_peek %x" CODE_RESET "\n",vm_env->code_ltv->data));
 done:
    TFINISH(vm_env->state,"");
    return;
}

static void vm_code_pop() { DEBUG(fprintf(ERRFILE,CODE_RED "  vm_code_pop %x" CODE_RESET "\n",vm_env->code_ltv->data));
    TSTART(vm_env->state,"");
    if (vm_env->code_ltvr)
        LTVR_release(&vm_env->code_ltvr->lnk);
    if (CLL_EMPTY(ENV_LIST(VMRES_CODE)))
        vm_env->state|=VM_COMPLETE;
    TFINISH(vm_env->state,"");
}

static void vm_resolve_at(CLL *cll,LTV *ref) {
    TSTART(vm_env->state,"");
    int status=0;
    LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
    void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
    CLL_map(cll,FWD,op);
 done:
    TFINISH(vm_env->state,"");
    return;
}

LTV *vm_resolve(LTV *ref) {
    vm_resolve_at(ENV_LIST(VMRES_DICT),ref);
    return REF_ltv(REF_HEAD(vm_env->ext));
}

void vm_dump_ltv(LTV *ltv,char *label) {
    TSTART(vm_env->state,"");
    char *filename;
    fprintf(OUTFILE,"%s\n",label);
    print_ltv(OUTFILE,CODE_RED,ltv,CODE_RESET "\n",0);
    graph_ltv_to_file(FORMATA(filename,32,"/tmp/%s.dot",label),ltv,0,label);
 done:
    TFINISH(vm_env->state,"");
    return;
}

static void vm_ffi(LTV *lambda) {
    CLL args; CLL_init(&args); // list of ffi arguments
    LTV *ftype=NULL,*cif=NULL,*rval=NULL;
    int void_func;

    THROW(!(ftype=LT_get(lambda,TYPE_BASE,HEAD,KEEP)),LTV_NULL);
    THROW(!(cif=cif_ffi_prep(ftype)),LTV_NULL);

    rval=cif_rval_create(ftype,NULL);
    if ((void_func=!rval))
        rval=LTV_NULL;
    int marshaller(char *name,LTV *type) {
        int status=0;
        LTV *arg=NULL, *coerced=NULL;
        STRY(!(arg=vm_stack_deq(POP)),"popping ffi arg (%s) from stack",name); // FIXME: attempt to resolve by name first
        STRY(!(coerced=cif_coerce_i2c(arg,type)),"coercing ffi arg (%s)",name);
        LTV_enq(&args,coerced,HEAD); // enq coerced arg onto args CLL
        LT_put(rval,name,HEAD,coerced); // coerced args are installed as childen of rval
        LTV_release(arg);
    done:
        return status;
    }
    TSTART(vm_env->state,"");
    THROW(cif_args_marshal(ftype,REV,marshaller),LTV_NULL); // pre-
    THROW(cif_ffi_call(cif,lambda->data,rval,&args),LTV_NULL);
    if (void_func)
        LTV_release(rval);
    else
        THROW(!vm_stack_enq(cif_coerce_c2i(rval)),LTV_NULL);
    CLL_release(&args,LTVR_release); //  ALWAYS release at end, to give other code a chance to enq an LTV
 done:
    TFINISH(vm_env->state,"");
    return;
}

static void vm_eval_cvar(LTV *cvar) {
    TSTART(vm_env->state,"");
    vm_reset_ext(); // sanitize
    if (cvar->flags&LT_TYPE) {
        LTV *type;
        THROW(!(type=cif_create_cvar(cvar,NULL,NULL)),LTV_NULL);
        THROW(!vm_stack_enq(type),LTV_NULL);
    } else
        vm_ffi(cvar); // if not a type, it could be a function
    vm_reset_ext(); // sanitize again
 done:
    TFINISH(vm_env->state,"");
    return;
}

void vm_eval_ltv(LTV *ltv) {
    THROW(!ltv,LTV_NULL);
    if (ltv->flags&LT_CVAR) // type, ffi, ...
        vm_eval_cvar(ltv);
    else
        vm_code_push(compile_ltv(jit_edict,ltv));
    vm_env->state|=VM_YIELD;
 done: return;
}

extern void is_lit() {
    TSTART(vm_env->state,"");
    LTV *ltv;
    THROW(!(ltv=vm_stack_deq(POP)),LTV_NULL);
    THROW(ltv->flags&LT_NSTR,LTV_NULL); // throw if non-string
    int tlen=series(ltv->data,ltv->len,WHITESPACE,NULL,NULL);
    THROW(tlen && ltv->len==tlen,LTV_NULL);
 done:
    LTV_release(ltv);
    TFINISH(vm_env->state,"");
    return;
}

extern void split() { // mini parser that pops a lit and pushes its CAR & CDR
    TSTART(vm_env->state,"");
    LTV *ltv=NULL;
    char *tdata=NULL;
    int len=0,tlen=0;

    int advance(int adv) { adv=MIN(adv,len); tdata+=adv; len-=adv; return adv; }

    THROW(!(ltv=vm_stack_deq(POP)),LTV_NULL); // ,"popping ltv to split");
    THROW(ltv->flags&LT_NSTR,LTV_NULL); // throw if non-string
    if (!ltv->len) {
        THROW(1,LTV_NULL);
        LTV_release(ltv);
    }
    tdata=ltv->data;
    len=ltv->len;
    advance(series(tdata,len,WHITESPACE,NULL,NULL)); // skip whitespace
    if ((tlen=series(tdata,len,NULL,WHITESPACE,"[]"))) {
        LTV *car=LTV_init(NEW(LTV),tdata,tlen,LT_DUP); // CAR
        advance(tlen);
        advance(series(tdata,len,WHITESPACE,NULL,NULL)); // skip whitespace
        LTV *cdr=LTV_init(NEW(LTV),tdata,len,LT_DUP); // CDR
        vm_stack_enq(cdr);
        vm_stack_enq(car);
    }
 done:
    LTV_release(ltv);
    TFINISH(vm_env->state,"");
    return;
}

extern void stack() { vm_dump_ltv(vm_deq(VMRES_STACK,KEEP),res_name[VMRES_STACK]); }
extern void locals() { vm_dump_ltv(vm_deq(VMRES_DICT,KEEP),res_name[VMRES_DICT]); }
extern void dict() { vm_dump_ltv(&vm_env->ltv[VMRES_DICT],res_name[VMRES_DICT]); }

LTV *encaps_ltv(LTV *ltv) {
    TSTART(vm_env->state,"");
    LTV *ltvltv=NULL;
    THROW(!(ltvltv=cif_create_cvar(cif_type_info("(LTV)*"),NULL,NULL)),LTV_NULL); // allocate an LTV *
    (*(LTV **) ltvltv->data)=ltv; // ltvltv->data is a pointer to an LTV *
    THROW(!LT_put(ltvltv,"TYPE_CAST",HEAD,ltv),LTV_NULL);
 done:
    TFINISH(vm_env->state,"");
    return ltvltv;
}

extern void encaps() {
    TSTART(vm_env->state,"");
    THROW(!(vm_stack_enq(encaps_ltv(vm_stack_deq(POP)))),LTV_NULL);
 done:
    TFINISH(vm_env->state,"");
    return;
}

LTV *decaps_ltv(LTV *ltv) { return ltv; } // all the work is done via coersion

extern void decaps() {
    TSTART(vm_env->state,"");
    LTV *cvar_ltv=NULL,*ptr=NULL;
    THROW(!(cvar_ltv=vm_stack_deq(POP)),LTV_NULL);
    THROW(!(cvar_ltv->flags&LT_CVAR),LTV_NULL); // "checking at least if it's a cvar" // TODO: verify it's an "(LTV)*"
    THROW(!(ptr=*(LTV **) (cvar_ltv->data)),LTV_NULL);
    THROW(!(vm_stack_enq(ptr)),LTV_NULL);
 done:
    LTV_release(cvar_ltv);
    TFINISH(vm_env->state,"");
    return;
}

extern void vm_while(LTV *lambda) {
    TSTART(vm_env->state,"");
    while (!vm_env->state)
        vm_eval_ltv(lambda);
    TFINISH(vm_env->state,"");
}

extern void dup() {
    TSTART(vm_env->state,"");
    THROW(!vm_stack_enq(vm_stack_deq(KEEP)),LTV_NULL);
 done:
    TFINISH(vm_env->state,"");
    return;
}

//////////////////////////////////////////////////
// Opcode Handlers
//////////////////////////////////////////////////

#define VMOP_DEBUG() DEBUG(debug(__func__); fprintf(ERRFILE,"%d %d %s\n",vm_env->state,vm_env->skipdepth,__func__)); TOPCODE(vm_env->state);
#define SKIP_IF_STATE() do { if (vm_env->state) { DEBUG(fprintf(ERRFILE,"  (skipping %s)\n",__func__)); goto done; }} while(0);

static void vmop_RESET() { VMOP_DEBUG();
    SKIP_IF_STATE();
    vm_reset_ext();
 done: return;
}

static void vmop_THROW() { VMOP_DEBUG();
    SKIP_IF_STATE();
    if (vm_use_ext())
        THROW(1,vm_env->ext);
    else
        THROW(1,LTV_NULL);
 done: return;
}

static void vmop_CATCH() { VMOP_DEBUG();
    int status=0;
    if (!vm_env->state)
        vm_env->state|=VM_BYPASS;
    else if ((vm_env->state|VM_THROWING) && (!vm_env->skipdepth)) {
        LTV *exception=NULL;
        STRY(!(exception=vm_deq(VMRES_EXCP,KEEP)),"peeking at exception stack");
        if (vm_use_ext()) {
            vm_env->ext=REF_create(vm_env->ext);
            vm_resolve(vm_env->ext);
            THROW(!vm_stack_enq(REF_ltv(REF_HEAD(vm_env->ext))),LTV_NULL);
            if (exception==REF_ltv(REF_HEAD(vm_env->ext))) {
                LTV_release(vm_deq(VMRES_EXCP,POP));
                if (!vm_deq(VMRES_EXCP,KEEP)) // if no remaining exceptions
                    vm_env->state&=~VM_THROWING;
            }
        }
        else { // catch all
            while (exception=vm_deq(VMRES_EXCP,POP)) // empty remaining exceptions
                LTV_release(exception);
            vm_env->state&=~VM_THROWING;
        }
    }
 done: return;
}

static void vmop_PUSHEXT() { VMOP_DEBUG();
    SKIP_IF_STATE();
    if (vm_use_ext()) {
        vm_stack_enq(vm_env->ext);
        if (vm_env->ext->flags&LT_REFS)
            vm_resolve(vm_env->ext);
    } else {
        LTV *stack;
        THROW(!(stack=vm_deq(VMRES_STACK,KEEP)),LTV_NULL);
        LTV *dest=NULL;
        if ((dest=vm_stack_deq(KEEP)) && (dest->flags&LT_REFS) && (dest=vm_stack_deq(POP))) { // merge stack into ref
            if (!REF_lti(REF_HEAD(dest))) // create LTI if it's not already resolved
                REF_resolve(vm_deq(VMRES_DICT,KEEP),dest,TRUE);
            CLL_MERGE(&(REF_lti(REF_HEAD(dest))->ltvs),LTV_list(stack),REF_HEAD(dest)->reverse);
            vm_stack_enq(dest);
        }
    }
 done: return;
}

static void vmop_EVAL() { VMOP_DEBUG();
    SKIP_IF_STATE();
    vm_eval_ltv(vm_stack_deq(POP));
 done: return;
}


static void vmop_REF() { VMOP_DEBUG();
    SKIP_IF_STATE();
    if (vm_use_ext())
        vm_env->ext=REF_create(vm_env->ext);
 done: return;
}

static void vmop_DEREF() { VMOP_DEBUG();
    SKIP_IF_STATE();
    vm_resolve(vm_use_ext());
    LTV *ltv=REF_ltv(REF_HEAD(vm_env->ext));
    if (!ltv)
        ltv=LTV_dup(vm_env->ext);
    if (ltv->flags&LT_REFS) {
        LTV *ref=ltv;
        THROW(!(ltv=REF_ltv(REF_HEAD(ref))),LTV_NULL);
        REF_iterate(ref,0);
    }
    THROW(!vm_stack_enq(ltv),LTV_NULL);
 done: return;
}

static void vmop_ASSIGN() { VMOP_DEBUG();
    SKIP_IF_STATE();
    LTV *val;
    THROW(!(val=vm_stack_deq(POP)),LTV_NULL);
    if (!vm_use_ext()) { // assign to TOS
        LTV *var;
        THROW(!(var=vm_stack_deq(POP)),LTV_NULL);
        if (var->flags&LT_CVAR) { // if TOS is a cvar
            THROW(!vm_stack_enq(cif_assign_cvar(var,val)),LTV_NULL); // assign directly to it.
        } else {
            vm_env->ext=REF_create(var); // otherwise treat TOS as a reference...
            REF_resolve(vm_deq(VMRES_DICT,KEEP),vm_env->ext,TRUE);
            if (REF_ltv(REF_HEAD(vm_env->ext))) // ...and it already exists...
                THROW(REF_replace(vm_env->ext,val),LTV_NULL); // ...REPLACE its value
            else
                THROW(REF_assign(vm_env->ext,val),LTV_NULL); // ...else install it
        }
    } else {
        REF_resolve(vm_deq(VMRES_DICT,KEEP),vm_env->ext,TRUE);
        THROW(REF_assign(vm_env->ext,val),LTV_NULL);
    }
 done: return;
}

static void vmop_REMOVE() { VMOP_DEBUG();
    SKIP_IF_STATE();
    if (vm_use_ext()) {
        vm_resolve(vm_env->ext);
        REF_remove(vm_env->ext);
    } else {
        LTV_release(vm_stack_deq(POP));
    }
 done: return;
}

static void vmop_CTX_PUSH() { VMOP_DEBUG();
    if (vm_env->state) { DEBUG(fprintf(ERRFILE,"  (skipping %s)\n",__func__));
        vm_env->skipdepth++;
    } else {
        THROW(!vm_enq(VMRES_DICT,vm_stack_deq(POP)),LTV_NULL);
        THROW(!vm_enq(VMRES_STACK,LTV_NULL_LIST),LTV_NULL);
    }
 done: return;
}

static void vmop_CTX_POP() { VMOP_DEBUG();
    if (vm_env->skipdepth>0) { DEBUG(fprintf(ERRFILE,"  (deskipping %s)\n",__func__));
        vm_env->skipdepth--;
    } else {
        vm_listcat(VMRES_STACK);
        THROW(!vm_stack_enq(vm_deq(VMRES_DICT,POP)),LTV_NULL);
    }
 done: return;
}

static void vmop_FUN_PUSH() { VMOP_DEBUG();
    if (vm_env->state) { DEBUG(fprintf(ERRFILE,"  (skipping %s)\n",__func__));
        vm_env->skipdepth++;
    } else {
        THROW(!vm_enq(VMRES_FUNC,vm_stack_deq(POP)),LTV_NULL);
        THROW(!vm_enq(VMRES_STACK,LTV_NULL_LIST),LTV_NULL);
        THROW(!vm_enq(VMRES_DICT,LTV_NULL),LTV_NULL);
    }
 done: return;
}

static void vmop_FUN_EVAL() { VMOP_DEBUG();
    if (vm_env->skipdepth>0) { DEBUG(fprintf(ERRFILE,"  (deskipping %s)\n",__func__));
        vm_env->skipdepth--;
    } else {
        vm_code_push(fun_pop_bc); // stack signal to CTX_POP and REMOVE after eval
        vm_eval_ltv(vm_deq(VMRES_FUNC,POP));
    }
 done: return;
}

// never skip fun_pop_bc
static void vmop_FUN_POP() { VMOP_DEBUG();
    vm_listcat(VMRES_STACK);
    LTV_release(vm_deq(VMRES_DICT,POP));
 done: return;
}

static void vmop_S2S() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_stack_deq(KEEP)),LTV_NULL);
 done: return;
}

static void vmop_D2S() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_deq(VMRES_DICT,KEEP)),LTV_NULL);
 done: return;
}

static void vmop_E2S() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_deq(VMRES_EXCP,KEEP)),LTV_NULL);
 done: return;
}

static void vmop_F2S() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_deq(VMRES_FUNC,KEEP)),LTV_NULL);
 done: return;
}

static void vmop_S2D() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_deq(VMRES_DICT,POP)),LTV_NULL);
 done: return;
}

static void vmop_S2E() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_deq(VMRES_EXCP,POP)),LTV_NULL);
 done: return;
}

static void vmop_S2F() { VMOP_DEBUG();
    SKIP_IF_STATE();
    THROW(!vm_stack_enq(vm_deq(VMRES_FUNC,POP)),LTV_NULL);
 done: return;
}

//////////////////////////////////////////////////

typedef void (*VMOP_CALL)();
extern VMOP_CALL vmop_call[];

#define OPCODE (*(char *) vm_env->code_ltv->data)
#define ADVANCE(INC) do { vm_env->code_ltv->data+=(INC); vm_env->code_ltv->len-=(INC); } while (0)

static void vmop_EXT() { VMOP_DEBUG();
    if (vm_env->ext) // slimmed reset_ext()
        LTV_release(vm_env->ext);
    vm_env->ext=NULL;
    vm_env->ext_length=ntohl(*(unsigned *) vm_env->code_ltv->data); ADVANCE(sizeof(unsigned));
    vm_env->ext_flags=ntohl(*(unsigned *)  vm_env->code_ltv->data); ADVANCE(sizeof(unsigned));
    vm_env->ext_data=vm_env->code_ltv->data;                        ADVANCE(vm_env->ext_length);
    TOPEXT(vm_env->ext_data,vm_env->ext_length,vm_env->ext_flags,vm_env->state);
    DEBUG(fprintf(ERRFILE,CODE_BLUE " ");
          fprintf(ERRFILE,"len %d flags %x state %x; -----> ",vm_env->ext_length,vm_env->ext_flags,vm_env->state);
          fstrnprint(ERRFILE,vm_env->ext_data,vm_env->ext_length);
          fprintf(ERRFILE,CODE_RESET "\n"));
 done: return;
}

//////////////////////////////////////////////////

VMOP_CALL vmop_call[] = {
                         vmop_RESET,
                         vmop_EXT,
                         vmop_THROW,
                         vmop_CATCH,
                         vmop_PUSHEXT,
                         vmop_EVAL,
                         vmop_REF,
                         vmop_DEREF,
                         vmop_ASSIGN,
                         vmop_REMOVE,
                         vmop_CTX_PUSH,
                         vmop_CTX_POP,
                         vmop_FUN_PUSH,
                         vmop_FUN_EVAL,
                         vmop_FUN_POP,
                         vmop_S2S,
                         vmop_D2S,
                         vmop_E2S,
                         vmop_F2S,
                         vmop_S2D,
                         vmop_S2E,
                         vmop_S2F,
};

//////////////////////////////////////////////////
// Opcode Dispatch

static VMOP_CALL vm_dispatch(VMOP_CALL call) {
    ADVANCE(1);
    call();
    if (vm_env->code_ltv->len<=0) {
        vm_env->state|=VM_YIELD;
        vm_env->state&=~VM_BYPASS;
        vm_code_pop();
    }
    return vm_env->state?NULL:vmop_call[OPCODE];
}

static int vm_run() {
    vm_code_push(compile(jit_edict,"CODE!",-1));
    while (!(vm_env->state&(VM_COMPLETE|VM_ERROR))) {
        vm_reset_ext();
        vm_code_peek();
        VMOP_CALL call=vmop_call[OPCODE];
        while (call=vm_dispatch(call)); // inner loop
        vm_env->state&=~(VM_YIELD);
    }
 done:
    return vm_env->state;
}

//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////

// Translate a prepared C callback into an edict function/environment and evaluate it
static void vm_closure_thunk(ffi_cif *CIF,void *RET,void **ARGS,void *USER_DATA)
{
    int status=0;
    char *argid=NULL;

    if (!vm_env_stack)
        vm_env_stack=LTV_NULL_LIST;

    LTV *continuation=(LTV *) USER_DATA;

    LTV *env_cvar=cif_create_cvar(cif_type_info("VM_ENV"),NEW(VM_ENV),NULL);
    STRY(!env_cvar,"validating creation of env cvar");

    LTV_put(LTV_list(vm_env_stack),env_cvar,HEAD,NULL);
    vm_env=(VM_ENV *) env_cvar->data;

    {
        vm_env->state=0;

        for (int i=0;i<VMRES_COUNT;i++)
            LTV_init(&vm_env->ltv[i],NULL,0,LT_NULL|LT_LIST);

        STRY(!vm_enq(VMRES_STACK,LTV_NULL_LIST),"initializing env stack");
        STRY(!vm_enq(VMRES_DICT,env_cvar),"adding continuation to env");
        STRY(!vm_enq(VMRES_DICT,continuation),"adding continuation to env");

        LTV *locals=LTV_init(NEW(LTV),"THUNK_LOCALS",-1,LT_NONE);
        STRY(!vm_enq(VMRES_DICT,locals),"pushing locals into dict");

        int index=0;
        int marshaller(char *name,LTV *type) {
            LTV *arg=cif_create_cvar(type,ARGS[index],NULL);
            LT_put(locals,FORMATA(argid,32,"ARG%d",index++),HEAD,arg); // embed by index
            if (name)
                LT_put(locals,name,HEAD,arg); // embed by name
            return 0;
        }
        LTV *ffi_type_info=LT_get(continuation,TYPE_BASE,HEAD,KEEP);
        STRY(cif_args_marshal(ffi_type_info,REV,marshaller),"marshalling ffi args");
        LT_put(locals,"RETURN",HEAD,cif_rval_create(ffi_type_info,RET)); // embed return val cvar in environment; ok if it fails

        vm_run();
    }

    LTV_release(LTV_get(LTV_list(vm_env_stack),POP,HEAD,NULL,NULL));
    if ((env_cvar=LTV_get(LTV_list(vm_env_stack),KEEP,HEAD,NULL,NULL)))
        vm_env=(VM_ENV *) env_cvar->data;

 done:
    return;
}

extern LTV *vm_await(pthread_t thread) {
    LTV *result=NULL;
    pthread_join(thread,(void *) &result);
    return result;
}

typedef void *(*vm_thunk)(void *); // make sure a pthread thunk type is aliased

extern pthread_t vm_async(LTV *continuation,LTV *arg) {
    pthread_t thread;
    pthread_attr_t attr={};
    pthread_attr_init(&attr);
    THROW(pthread_create(&thread,&attr,(vm_thunk) continuation->data,(void *) arg),LTV_NULL);
 done:
    return thread;
}

extern LTV *vm_continuation(LTV *ffi_sig,LTV *root,LTV *code) {
    int status=0;

    LTV *continuation=NULL;
    THROW(!(continuation=cif_create_closure(ffi_sig,vm_closure_thunk)),LTV_NULL);
    LT_put(continuation,"CODE",HEAD,code);
    LT_put(continuation,"ROOT",HEAD,root);
 done:
    return continuation;
}

extern LTV *vm_eval(LTV *root,LTV *code,LTV *arg) {
    return vm_await(vm_async(vm_continuation(cif_type_info("(LTV)*(*)((LTV)*)"),root,code),arg));
}

extern int vm_bootstrap(char *bootstrap) {
    try_depth=0;
    LTV *rval=vm_eval(cif_module,LTV_init(NEW(LTV),bootstrap,-1,LT_NONE),LTV_NULL);
    if (rval)
        print_ltv(OUTFILE,"",rval,"\n",0);
    return !rval;
}
