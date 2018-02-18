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


#define _GNU_SOURCE
#define _C99
#include <setjmp.h>
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

jmp_buf vm_yield_loc; // to punch out of bytecode interpreter

pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

enum {
    VMRES_DICT,
    VMRES_STACK,
    VMRES_FUNC,
    VMRES_CTXT, // bypass/throw
    VMRES_CODE,
    VMRES_COUNT,
} VM_LISTRES;

char *res_name[] = { "VMRES_DICT","VMRES_STACK","VMRES_FUNC","VMRES_CTXT","VMRES_CODE" };

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

__thread VM_ENV *vm_env; // every thread can have a vm environment

//////////////////////////////////////////////////
void debug(const char *fromwhere) { return; }
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Basic Utillties

#define ENV_LIST(res) LTV_list(&vm_env->ltv[res])
#define THROW(expression,ltv) do { if (expression) { vm_enq(VMRES_CTXT,(ltv)); vm_reset_ext(); vm_env->state|=VM_THROWING; goto done; } } while(0)

LTV *vm_enq(int res,LTV *tos) { return LTV_put(ENV_LIST(res),tos,HEAD,NULL); }
LTV *vm_deq(int res,int pop)  { return LTV_get(ENV_LIST(res),pop,HEAD,NULL,NULL); }

LTV *vm_stack_enq(LTV *ltv) { return LTV_enq(LTV_list(vm_deq(VMRES_STACK,KEEP)),ltv,HEAD); }
LTV *vm_stack_deq(int pop) {
    void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
    LTV *rval=CLL_map(ENV_LIST(VMRES_STACK),FWD,op);
    return rval;
}

//////////////////////////////////////////////////
// Specialized Utillties

void vm_reset_ext() {
    vm_env->ext_length=vm_env->ext_flags=0;
    vm_env->ext_data=NULL;
    if (vm_env->ext)
        LTV_release(vm_env->ext);
    vm_env->ext=NULL;
}

LTV *vm_use_ext() {
    if (vm_env->ext_data && !vm_env->ext)
        vm_env->ext=LTV_init(NEW(LTV),vm_env->ext_data,vm_env->ext_length,vm_env->ext_flags);
    return vm_env->ext;
}

void vm_push_code(LTV *ltv) {
    THROW(!ltv,LTV_NULL);
    LTV *opcode_ltv=LTV_init(NEW(LTV),ltv->data,ltv->len,LT_BIN|LT_LIST);
    THROW(!opcode_ltv,LTV_NULL);
    THROW(!LTV_enq(LTV_list(opcode_ltv),ltv,HEAD),LTV_NULL); // encaps code ltv within tracking ltv
    THROW(!vm_enq(VMRES_CODE,opcode_ltv),LTV_NULL);
 done:
    return;
}

void vm_get_code() {
    vm_env->code_ltvr=NULL;
    THROW(!(vm_env->code_ltv=LTV_get(ENV_LIST(VMRES_CODE),KEEP,HEAD,NULL,&vm_env->code_ltvr)),LTV_NULL);
 done: return;
}

int vm_pop_code() { DEBUG(fprintf(stderr,CODE_RED "  POP CODE %x" CODE_RESET "\n",vm_env->code_ltv->data));
    if (vm_env->code_ltvr)
        LTVR_release(&vm_env->code_ltvr->lnk);
    return CLL_EMPTY(ENV_LIST(VMRES_CODE));
}

int vm_concat(int res) { // merge tos and nos LTVs
    int status=0;
    LTV *tos,*nos,*ltv;
    STRY(!(tos=vm_deq(res,POP)),"popping ext");
    STRY(!(nos=vm_deq(res,POP)),"peeking ext");
    STRY(!(ltv=LTV_concat(tos,nos)),"concatenating tos and ns");
    vm_enq(res,ltv);
    LTV_release(tos);
    LTV_release(nos);
 done:
    return status;
}

int vm_listcat(int res) { // merge tos into head of nos
    int status=0;
    LTV *tos,*nos;
    STRY(!(tos=vm_deq(res,POP)),"popping res %d",res);
    STRY(!(nos=vm_deq(res,KEEP)),"peeking res %d",res);
    STRY(!(tos->flags&LT_LIST && nos->flags&LT_LIST),"verifying lists");
    CLL_MERGE(LTV_list(nos),LTV_list(tos),HEAD);
    LTV_release(tos);
 done:
    return status;
}

int vm_ref_hres(CLL *cll,LTV *ref) {
    int status=0;
    LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
    void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
    CLL_map(cll,FWD,op);
 done:
    return status;
}

int vm_dump_ltv(LTV *ltv,char *label) {
    int status=0;
    char *filename;
    printf("%s\n",label);
    print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0);
    graph_ltv_to_file(FORMATA(filename,32,"/tmp/%s.dot",label),ltv,0,label);
 done:
    return status;
}

int vm_ffi(LTV *lambda) { // adapted from edict.c's ffi_eval(...)
    int status=0;
    CLL args; CLL_init(&args); // list of ffi arguments
    LTV *rval=NULL;
    int void_func;
    LTV *ftype=LT_get(lambda,TYPE_BASE,HEAD,KEEP);
    rval=cif_rval_create(ftype,NULL);
    if ((void_func=!rval))
        rval=LTV_NULL;
    int marshaller(char *name,LTV *type) {
        int status=0;
        LTV *arg=NULL, *coerced=NULL;
        STRY(!(arg=vm_stack_deq(POP)),"popping ffi arg (%s) from stack",name); // FIXME: attempt to resolve by name first
        STRY(!(coerced=cif_coerce_i2c(arg,type)),"coercing ffi arg");
        LTV_enq(&args,coerced,HEAD); // enq coerced arg onto args CLL
        LT_put(rval,name,HEAD,coerced); // coerced args are installed as childen of rval
        LTV_release(arg);
    done:
        return status;
    }
    STRY(cif_args_marshal(ftype,marshaller),"marshalling ffi args"); // pre-
    STRY(cif_ffi_call(ftype,lambda->data,rval,&args),"calling ffi");
    if (void_func)
        LTV_release(rval);
    else
        STRY(!vm_stack_enq(cif_coerce_c2i(rval)),"enqueing coerced rval onto stack");
    CLL_release(&args,LTVR_release); //  ALWAYS release at end, to give other code a chance to enq an LTV
 done:
    return status;
}

int vm_cvar(LTV *type) {
    int status=0;
    LTV *cvar;
    STRY(!type,"validating type");
    STRY(!(cvar=cif_create_cvar(type,NULL,NULL)),"creating cvar");
    STRY(!vm_stack_enq(cvar),"pushing cvar");
 done:
    return status;
}

void vm_eval_type(LTV *type) {
    if (type->flags&LT_TYPE)
        THROW(vm_cvar(type),type);
    else
        THROW(vm_ffi(type),type); // if not a type, it could be a function
 done:
    return;
}


extern void vm_import() {
    LTV *mod=NULL;
    THROW(cif_curate_module(vm_stack_deq(KEEP),false),LTV_NULL);
 done:
    return;
}

extern void vm_dump() {
    vm_dump_ltv(&vm_env->ltv[VMRES_DICT],res_name[VMRES_DICT]);
    vm_dump_ltv(&vm_env->ltv[VMRES_STACK],res_name[VMRES_STACK]);
    return;
}

extern void vm_locals() {
    int old_show_ref=show_ref;
    show_ref=1;
    vm_dump_ltv(vm_deq(VMRES_DICT,KEEP),res_name[VMRES_DICT]);
    vm_dump_ltv(vm_deq(VMRES_STACK,KEEP),res_name[VMRES_STACK]);
    show_ref=old_show_ref;
    return;
}

extern void vm_hoist() {
    LTV *ltv=NULL,*ltvltv=NULL;
    THROW(!(ltv=vm_stack_deq(POP)),LTV_NULL); // ,"popping ltv to hoist");
    THROW(!(ltvltv=cif_create_cvar(cif_type_info("(LTV)*"),NULL,NULL)),LTV_NULL); // allocate an LTV *
    (*(LTV **) ltvltv->data)=ltv; // ltvltv->data is a pointer to an LTV *
    THROW(!(LT_put(ltvltv,"TYPE_CAST",HEAD,ltv)),LTV_NULL);
    THROW(!(vm_stack_enq(ltvltv)),LTV_NULL); // ,"pushing hoisted ltv cvar");
 done:
    return;
}

extern void vm_plop() {
    LTV *cvar_ltv=NULL,*ptr=NULL;
    THROW(!(cvar_ltv=vm_stack_deq(POP)),LTV_NULL);
    THROW(!(cvar_ltv->flags&LT_CVAR),LTV_NULL); // "checking at least if it's a cvar" // TODO: verify it's an "(LTV)*"
    THROW(!(ptr=*(LTV **) (cvar_ltv->data)),LTV_NULL);
    THROW(!(vm_stack_enq(ptr)),LTV_NULL);
 done:
    LTV_release(cvar_ltv);
    return;
}

extern void vm_COMPARE() {
 done:
    return;
}

extern void vm_SPLIT() {
 done:
    return;
}

extern void vm_MERGE() { // even needed?
    LTV *tos,*nos;
    tos=vm_stack_deq(POP);
    nos=vm_stack_deq(POP);
    vm_stack_enq(LTV_concat(tos,nos));
    LTV_release(tos);
    LTV_release(nos);
 done:
    return;
}

extern void vm_ITER_POP() {
    if (!vm_env->state) {
        if (vm_use_ext())
            THROW(!vm_stack_enq(vm_env->ext),LTV_NULL);
    }
 done:
    return;
}

extern void vm_ITER_KEEP() {
    if (!vm_env->state) {
        if (vm_use_ext())
            THROW(!vm_stack_enq(vm_env->ext),LTV_NULL);
    }
 done:
    return;
}

extern void vm_eval_ltv(LTV *ltv) {
    THROW(!ltv,LTV_NULL);
    if (ltv->flags&LT_CVAR) // type, ffi, ...
        vm_eval_type(ltv);
    else {
        vm_push_code(compile_ltv(compilers[FORMAT_edict],ltv));
        vm_env->state|=VM_YIELD;
    }
 done: return;
}

//////////////////////////////////////////////////
// Opcode Handlers
//////////////////////////////////////////////////

typedef void (*VMOP_CALL)();
extern VMOP_CALL vmop_call[];

#define OPCODE (*(char *) vm_env->code_ltv->data)
#define ADVANCE(INC) do { vm_env->code_ltv->data+=(INC); vm_env->code_ltv->len-=(INC); } while (0)

//////////////////////////////////////////////////

#define VMOP_DEBUG() DEBUG(debug(__func__); fprintf(stderr,"%d %s\n",vm_env->state,__func__));

extern void vmop_YIELD() { VMOP_DEBUG();
    vm_env->state|=VM_YIELD;
    if (vm_env->code_ltv->len<=0)
        if (vm_pop_code())
            vm_env->state|=VM_COMPLETE;
}

extern void vmop_RESET() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    vm_reset_ext();
 done: return;
}

extern void vmop_NIL() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    vm_stack_enq(LTV_NULL);
 done: return;
}

extern void vmop_EXT() { VMOP_DEBUG();
    int status=0;
    if (vm_env->ext) // slimmed reset_ext()
        LTV_release(vm_env->ext);
    vm_env->ext=NULL;
    vm_env->ext_length=ntohl(*(unsigned *) vm_env->code_ltv->data); ADVANCE(sizeof(unsigned));
    vm_env->ext_flags=ntohl(*(unsigned *)  vm_env->code_ltv->data); ADVANCE(sizeof(unsigned));
    vm_env->ext_data=vm_env->code_ltv->data;                        ADVANCE(vm_env->ext_length);
    DEBUG(fprintf(stderr,CODE_BLUE " ");
          fprintf(stderr,"len %d flags %x state %x; -----> ",vm_env->ext_length,vm_env->ext_flags,vm_env->state);
          fstrnprint(stderr,vm_env->ext_data,vm_env->ext_length);
          fprintf(stderr,CODE_RESET "\n"));
 done: return;
}

extern void vmop_THROW() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    if (vm_use_ext())
        THROW(1,vm_env->ext);
    else
        THROW(1,LTV_NULL);
 done: return;
}

extern void vmop_CATCH() { VMOP_DEBUG();
    int status=0;
    if (!vm_env->state)
        vm_env->state|=VM_BYPASS;
    else if (vm_env->state|VM_THROWING) {
        LTV *exception=NULL;
        STRY(!(exception=vm_deq(VMRES_CTXT,KEEP)),"peeking at exception stack");
        if (vm_use_ext()) {
            vm_env->ext=REF_create(vm_env->ext);
            vm_ref_hres(ENV_LIST(VMRES_DICT),vm_env->ext);
            THROW(!vm_stack_enq(REF_ltv(REF_HEAD(vm_env->ext))),LTV_NULL);
            if (exception==REF_ltv(REF_HEAD(vm_env->ext))) {
                LTV_release(vm_deq(VMRES_CTXT,POP));
                if (!vm_deq(VMRES_CTXT,KEEP)) // if no remaining exceptions
                    vm_env->state&=~VM_THROWING;
            }
        }
        else { // catch all
            while (exception=vm_deq(VMRES_CTXT,POP)) // empty remaining exceptions
                LTV_release(exception);
            vm_env->state&=~VM_THROWING;
        }
    }
 done: return;
}

extern void vmop_PUSHEXT() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    if (vm_use_ext())
        vm_stack_enq(vm_env->ext);
 done: return;
}

extern void vmop_EVAL() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    vm_eval_ltv(vm_stack_deq(POP));
 done: return;
}


extern void vmop_REF() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    if (vm_use_ext())
        vm_env->ext=REF_create(vm_env->ext);
 done: return;
}

extern void vmop_DEREF() { VMOP_DEBUG();
    if (vm_env->state)
        goto done;
    vm_ref_hres(ENV_LIST(VMRES_DICT),vm_use_ext());
    LTV *ltv=REF_ltv(REF_HEAD(vm_env->ext));
    if (!ltv)
        ltv=LTV_dup(vm_env->ext);
    THROW(!vm_stack_enq(ltv),LTV_NULL);
 done: return;
}

extern void vmop_ASSIGN() { VMOP_DEBUG();
    if (vm_env->state)
        goto done;
    if (!vm_use_ext()) { // assign to TOS
        LTV *ltv;
        THROW(!(ltv=vm_stack_deq(POP)),LTV_NULL);
        if (ltv->flags&LT_CVAR) { // if TOS is a cvar
            THROW(!vm_stack_enq(cif_assign_cvar(ltv,vm_stack_deq(POP))),LTV_NULL); // assign directly to it.
            goto done;
        }
        vm_env->ext=REF_create(ltv); // otherwise treat TOS as a reference
    }
    REF_resolve(vm_deq(VMRES_DICT,KEEP),vm_env->ext,TRUE);
    THROW(REF_assign(REF_HEAD(vm_env->ext),vm_stack_deq(POP)),LTV_NULL);
 done: return;
}

extern void vmop_REMOVE() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    if (vm_use_ext()) {
        vm_ref_hres(ENV_LIST(VMRES_DICT),vm_env->ext);
        REF_remove(REF_HEAD(vm_env->ext));
    } else {
        LTV_release(vm_stack_deq(POP));
    }
 done: return;
}

extern void vmop_CTX_PUSH() { VMOP_DEBUG();
    if (vm_env->state) {
        vm_env->skipdepth++;
        goto done;
    }
    THROW(!vm_enq(VMRES_DICT,vm_stack_deq(POP)),LTV_NULL);
    THROW(!vm_enq(VMRES_STACK,LTV_NULL_LIST),LTV_NULL);
 done: return;
}

extern void vmop_CTX_POP() { VMOP_DEBUG();
    if (vm_env->state)
        if (--vm_env->skipdepth)
            goto done;
    THROW(vm_listcat(VMRES_STACK),LTV_NULL);
    THROW(!vm_stack_enq(vm_deq(VMRES_DICT,POP)),LTV_NULL);
 done: return;
}

extern void vmop_FUN_PUSH() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    THROW(!vm_enq(VMRES_FUNC,vm_stack_deq(POP)),LTV_NULL);
 done: return;
}

extern void vmop_FUN_EVAL() { VMOP_DEBUG();
    if (vm_env->state) goto done;
    vm_eval_ltv(vm_deq(VMRES_FUNC,POP));
 done: return;
}

VMOP_CALL vmop_call[] = {
    vmop_YIELD,
    vmop_RESET,
    vmop_NIL,
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
    vmop_FUN_EVAL
};

//////////////////////////////////////////////////
// Opcode Dispatch

static VMOP_CALL dispatch(VMOP_CALL call) {
    ADVANCE(1);
    call();
    return vm_env->state?vmop_YIELD:vmop_call[OPCODE];
}

int vm_eval() {
    while (!(vm_env->state&(VM_COMPLETE|VM_ERROR))) {
        vm_reset_ext();
        vm_get_code();
        VMOP_CALL call=vmop_call[OPCODE];
        do {
            call=dispatch(call);
        } while (call!=vmop_YIELD);
        vm_env->state&=~(VM_YIELD|VM_BYPASS);
    }
 done:
    return vm_env->state;
}

//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////

// Translate a prepared C callback into an edict function/environment and evaluate it
void vm_cb_thunk(ffi_cif *CIF,void *RET,void **ARGS,void *USER_DATA)
{
    int status=0;
    char *argid=NULL;

    LTV *env_cvar=(LTV *) USER_DATA;
    LTV *ffi_type_info=LT_get(env_cvar,"FFI_CIF_LTV",HEAD,KEEP);

    LTV *locals=LTV_init(NEW(LTV),"THUNK_LOCALS",-1,LT_NONE);
    STRY(!vm_enq(VMRES_DICT,locals),"pushing locals into dict");

    LT_put(locals,"RETURN",HEAD,cif_rval_create(ffi_type_info,RET)); // embed return val cvar in environment; ok if it fails

    int index=0;
    int marshaller(char *name,LTV *type) {
        LTV *arg=cif_create_cvar(type,ARGS[index],NULL);
        LT_put(locals,FORMATA(argid,32,"Arg%d",index++),HEAD,arg); // embed by index
        if (name)
            LT_put(locals,name,HEAD,arg); // embed by name
        return 0;
    }
    STRY(cif_args_marshal(ffi_type_info,marshaller),"marshalling ffi args");

    vm_eval();

 done:
    return;
}


extern void *vm_create_cb(char *callback_type,LTV *root,LTV *code)
{
    int status=0;

    if (vm_env) // this thread already has a vm
        return NULL;

    LTV *ffi_cif_ltv=cif_find_function(cif_type_info(callback_type));
    STRY(!ffi_cif_ltv,"validating callback type");

    LTV *env_cvar=cif_create_cvar(cif_type_info("VM_ENV"),NEW(VM_ENV),NULL);
    STRY(!env_cvar,"validating creation of env cvar");

    //////////////////////////////////////////////////
    vm_env=env_cvar->data;
    //////////////////////////////////////////////////

    for (int i=0;i<VMRES_COUNT;i++)
        LTV_init(&vm_env->ltv[i],NULL,0,LT_NULL|LT_LIST);
    vm_env->state=0;

    LTV *dict_anchor=LTV_NULL;
    LT_put(dict_anchor,"ENV",HEAD,env_cvar);

    vm_push_code(code); // ,"pushing code");
    STRY(!vm_enq(VMRES_DICT,dict_anchor),"adding anchor to dict");
    STRY(!vm_enq(VMRES_DICT,root),"addning reflection to dict");
    STRY(!vm_enq(VMRES_STACK,LTV_NULL_LIST),"initializing stack");

    STRY(cif_create_cb(ffi_cif_ltv,vm_cb_thunk,env_cvar),"creating vm environment/callback");
    LTV *exec_ltv=LT_get(env_cvar,"EXECUTABLE",HEAD,KEEP);

 done:
    if (status) {
        LTV_release(env_cvar);
        env_cvar=NULL;
    }
    return exec_ltv->data;
}


typedef int (*vm_bootstrap)();

int vm_boot()
{
    char *bootstrap_code=
        "[bootstrap.edict] [r] file_open! @bootfile\n"
        "[bootfile brl! ! bootloop!]@bootloop\n"
        "bootloop() | 0@RETURN\n";
    LTV *code=compile(compilers[FORMAT_edict],bootstrap_code,strlen(bootstrap_code));
    vm_bootstrap bootstrap=(vm_bootstrap) vm_create_cb("vm_bootstrap",cif_module,code);
    try_depth=0;
    int result=bootstrap();
    printf("Result: %d\n",result);
    return 0;
}

// ideas:
// generic a->b djykstra

/*
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
