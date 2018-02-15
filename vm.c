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
    VMRES_CODE,
    VMRES_OPCODE,
    VMRES_STACK,
    VMRES_EXC,
    VMRES_COUNT,
} VM_LISTRES;

char *res_name[] = { "VMRES_DICT","VMRES_CODE","VMRES_OPCODE","VMRES_STACK","VMRES_EXC" };

enum {
    VM_BYPASS   = 0x1,
    VM_THROWING = 0x2,
    VM_COMPLETE = 0x4,
    VM_ERROR    = 0x8,
    VM_HALT     = VM_COMPLETE | VM_ERROR
} VM_STATE;


typedef struct {
    LTV ltv[VMRES_COUNT];
    int state;
    unsigned ext_length;
    unsigned ext_flags;
    char *ext_data;
    LTV *wip;
    LTV *code_ltv,  *opcode_ltv;
    LTVR *code_ltvr,*opcode_ltvr;
} VM_ENV;


//////////////////////////////////////////////////
void debug(const char *fromwhere) { return; }
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Basic Utillties

#define ENV_LIST(res) LTV_list(&vm_env->ltv[res])

#define YIELD do { DEBUG(fprintf(stderr,"yield %s --------------\n",__func__)); return; } while (0)
#define ERROR(expression) do { if (expression) { vm_env->state|=VM_ERROR; DEBUG(fprintf(stderr,"error %s %s --------------\n",__func__,#expression)); return; } } while(0)
#define THROW(expression,ltv) do { if (expression) { ERROR(!vm_enq(vm_env,VMRES_EXC,(ltv))); vm_reset_wip(vm_env); vm_env->state|=VM_THROWING; goto done; } } while(0)

LTV *vm_enq(VM_ENV *vm_env,int res,LTV *tos) { return LTV_put(ENV_LIST(res),tos,HEAD,NULL); }
LTV *vm_deq(VM_ENV *vm_env,int res,int pop)  { return LTV_get(ENV_LIST(res),pop,HEAD,NULL,NULL); }

LTV *vm_stack_enq(VM_ENV *vm_env,LTV *ltv) { return LTV_enq(LTV_list(vm_deq(vm_env,VMRES_STACK,KEEP)),ltv,HEAD); }
LTV *vm_stack_deq(VM_ENV *vm_env,int pop) {
    void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
    LTV *rval=CLL_map(ENV_LIST(VMRES_STACK),FWD,op);
    return rval;
}

//////////////////////////////////////////////////
// Specialized Utillties

void vm_reset_wip(VM_ENV *vm_env) {
    vm_env->ext_length=vm_env->ext_flags=0;
    vm_env->ext_data=NULL;
    if (vm_env->wip)
        LTV_release(vm_env->wip);
    vm_env->wip=NULL;
}

LTV *vm_use_wip(VM_ENV *vm_env) {
    if (vm_env->ext_data && !vm_env->wip)
        vm_env->wip=LTV_init(NEW(LTV),vm_env->ext_data,vm_env->ext_length,vm_env->ext_flags);
    DEBUG(print_ltv(stdout,CODE_RED "use wip: ",vm_env->wip,CODE_RESET "\n",0));
    return vm_env->wip;
}

void vm_push_code(VM_ENV *vm_env,LTV *ltv) { DEBUG(fprintf(stderr,CODE_RED "  PUSH CODE %x" CODE_RESET "\n",ltv->data));
    ERROR(!vm_enq(vm_env,VMRES_CODE,ltv));
    ERROR(!vm_enq(vm_env,VMRES_OPCODE,LTV_init(NEW(LTV),ltv->data,1,LT_NONE)));
}

void vm_get_code(VM_ENV *vm_env) {
    vm_env->code_ltvr=vm_env->opcode_ltvr=NULL;
    vm_env->code_ltv  =LTV_get(ENV_LIST(VMRES_CODE),  KEEP,HEAD,NULL,&vm_env->code_ltvr);
    vm_env->opcode_ltv=LTV_get(ENV_LIST(VMRES_OPCODE),KEEP,HEAD,NULL,&vm_env->opcode_ltvr);
}

int vm_pop_code(VM_ENV *vm_env) { DEBUG(fprintf(stderr,CODE_RED "  POP CODE %x" CODE_RESET "\n",vm_env->code_ltv->data));
    if (vm_env->code_ltvr)
        LTVR_release(&vm_env->code_ltvr->lnk);
    if (vm_env->opcode_ltvr)
        LTVR_release(&vm_env->opcode_ltvr->lnk);
    return CLL_EMPTY(ENV_LIST(VMRES_CODE));
}

int vm_concat(VM_ENV *vm_env,int res) { // merge tos and nos LTVs
    int status=0;
    LTV *tos,*nos,*ltv;
    STRY(!(tos=vm_deq(vm_env,res,POP)),"popping wip");
    STRY(!(nos=vm_deq(vm_env,res,POP)),"peeking wip");
    STRY(!(ltv=LTV_concat(tos,nos)),"concatenating tos and ns");
    vm_enq(vm_env,res,ltv);
    LTV_release(tos);
    LTV_release(nos);
 done:
    return status;
}

int vm_listcat(VM_ENV *vm_env,int res) { // merge tos into head of nos
    int status=0;
    LTV *tos,*nos;
    STRY(!(tos=vm_deq(vm_env,res,POP)),"popping res %d",res);
    STRY(!(nos=vm_deq(vm_env,res,KEEP)),"peeking res %d",res);
    STRY(!(tos->flags&LT_LIST && nos->flags&LT_LIST),"verifying lists");
    CLL_MERGE(LTV_list(nos),LTV_list(tos),HEAD);
    LTV_release(tos);
 done:
    return status;
}

int vm_ref_hres(VM_ENV *vm_env,CLL *cll,LTV *ref) {
    int status=0;
    LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
    void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
    CLL_map(cll,FWD,op);
 done:
    return status;
}

int vm_dump_ltv(VM_ENV *vm_env,LTV *ltv,char *label) {
    int status=0;
    char *filename;
    printf("%s\n",label);
    print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0);
    graph_ltv_to_file(FORMATA(filename,32,"/tmp/%s.dot",label),ltv,0,label);
 done:
    return status;
}

int vm_ffi(VM_ENV *vm_env,LTV *lambda) { // adapted from edict.c's ffi_eval(...)
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
        STRY(!(arg=vm_stack_deq(vm_env,POP)),"popping ffi arg (%s) from stack",name); // FIXME: attempt to resolve by name first
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
        STRY(!vm_stack_enq(vm_env,cif_coerce_c2i(rval)),"enqueing coerced rval onto stack");
    CLL_release(&args,LTVR_release); //  ALWAYS release at end, to give other code a chance to enq an LTV
 done:
    return status;
}

int vm_cvar(VM_ENV *vm_env,LTV *type) {
    int status=0;
    LTV *cvar;
    STRY(!type,"validating type");
    STRY(!(cvar=cif_create_cvar(type,NULL,NULL)),"creating cvar");
    STRY(!vm_stack_enq(vm_env,cvar),"pushing cvar");
 done:
    return status;
}

void vm_eval_type(VM_ENV *vm_env,LTV *type) {
    if (type->flags&LT_TYPE)
        THROW(vm_cvar(vm_env,type),type);
    else
        THROW(vm_ffi(vm_env,type),type); // if not a type, it could be a function
 done:
    return;
}


extern void vm_import(VM_ENV *vm_env) {
    LTV *mod=NULL;
    THROW(cif_curate_module(vm_stack_deq(vm_env,KEEP),false),LTV_NULL);
 done:
    return;
}

extern void vm_dump(VM_ENV *vm_env) {
    vm_dump_ltv(vm_env,&vm_env->ltv[VMRES_DICT],res_name[VMRES_DICT]);
    vm_dump_ltv(vm_env,&vm_env->ltv[VMRES_STACK],res_name[VMRES_STACK]);
    return;
}

extern void vm_locals(VM_ENV *vm_env) {
    int old_show_ref=show_ref;
    show_ref=1;
    vm_dump_ltv(vm_env,vm_deq(vm_env,VMRES_DICT,KEEP),res_name[VMRES_DICT]);
    vm_dump_ltv(vm_env,vm_deq(vm_env,VMRES_STACK,KEEP),res_name[VMRES_STACK]);
    show_ref=old_show_ref;
    return;
}

extern void vm_hoist(VM_ENV *vm_env) {
    LTV *ltv=NULL,*ltvltv=NULL;
    THROW(!(ltv=vm_stack_deq(vm_env,POP)),LTV_NULL); // ,"popping ltv to hoist");
    THROW(!(ltvltv=cif_create_cvar(cif_type_info("(LTV)*"),NULL,NULL)),LTV_NULL); // allocate an LTV *
    (*(LTV **) ltvltv->data)=ltv; // ltvltv->data is a pointer to an LTV *
    THROW(!(LT_put(ltvltv,"TYPE_CAST",HEAD,ltv)),LTV_NULL);
    THROW(!(vm_stack_enq(vm_env,ltvltv)),LTV_NULL); // ,"pushing hoisted ltv cvar");
 done:
    return;
}

extern void vm_plop(VM_ENV *vm_env) {
    LTV *cvar_ltv=NULL,*ptr=NULL;
    THROW(!(cvar_ltv=vm_stack_deq(vm_env,POP)),LTV_NULL);
    THROW(!(cvar_ltv->flags&LT_CVAR),LTV_NULL); // "checking at least if it's a cvar" // TODO: verify it's an "(LTV)*"
    THROW(!(ptr=*(LTV **) (cvar_ltv->data)),LTV_NULL);
    THROW(!(vm_stack_enq(vm_env,ptr)),LTV_NULL);
 done:
    LTV_release(cvar_ltv);
    return;
}

//////////////////////////////////////////////////
// Opcode Handlers
//////////////////////////////////////////////////

typedef void (*VMOP_CALL)(VM_ENV *);

extern VMOP_CALL vmop_call[];

#define OPCODE(OP) (*(char *) (vm_env->opcode_ltv->data OP))
#define NEXTCALL  vmop_call[OPCODE(++)]

#define VMOP_DEBUG() DEBUG(debug(__func__); fprintf(stderr,"%s (code %x len %d loc %x offset %d nextop %x state %x)\n",__func__,vm_env->code_ltv->data,vm_env->code_ltv->len,vm_env->opcode_ltv->data,(vm_env->opcode_ltv->data-vm_env->code_ltv->data),OPCODE(),vm_env->state));

void vmop_YIELD(VM_ENV *vm_env) { VMOP_DEBUG();
    if ((vm_env->opcode_ltv->data - vm_env->code_ltv->data)>=vm_env->code_ltv->len)
        if (vm_pop_code(vm_env))
            vm_env->state|=VM_COMPLETE;
}

void vmop_EXT(VM_ENV *vm_env) { DEBUG(fprintf(stderr,"%s\n ",__func__));
    vm_env->ext_length=ntohl(*(unsigned *) vm_env->opcode_ltv->data); vm_env->opcode_ltv->data+=sizeof(unsigned);
    vm_env->ext_flags=ntohl(*(unsigned *)  vm_env->opcode_ltv->data); vm_env->opcode_ltv->data+=sizeof(unsigned);
    vm_env->ext_data=vm_env->opcode_ltv->data;                        vm_env->opcode_ltv->data+=vm_env->ext_length;
    DEBUG(fprintf(stderr,CODE_BLUE " "); fprintf(stderr,"len %d flags %x state %x; -----> ",vm_env->ext_length,vm_env->ext_flags,vm_env->state); fstrnprint(stdout,vm_env->ext_data,vm_env->ext_length); fprintf(stderr,CODE_RESET "\n"));
    NEXTCALL(vm_env);
}

void vmop_CTX_POP(VM_ENV *vm_env) { VMOP_DEBUG();
    THROW(vm_listcat(vm_env,VMRES_STACK),LTV_NULL);
    LTV_release(vm_deq(vm_env,VMRES_DICT,POP));
 done:
    NEXTCALL(vm_env);
}

void vmop_CTX_KEEP(VM_ENV *vm_env) { VMOP_DEBUG();
    int status=0;
    STRY(vm_listcat(vm_env,VMRES_STACK),"collapsing stack levels");
    STRY(!vm_stack_enq(vm_env,vm_deq(vm_env,VMRES_DICT,POP)),"returning dict level to stack");
 done:
    NEXTCALL(vm_env);
}

void vmop_CATCH(VM_ENV *vm_env) { VMOP_DEBUG();
    int status=0;
    if (!vm_env->state)
        vm_env->state|=VM_BYPASS;
    else if (vm_env->state|VM_THROWING) {
        LTV *exception=NULL;
        STRY(!(exception=vm_deq(vm_env,VMRES_EXC,KEEP)),"peeking at exception stack");
        if (vm_use_wip(vm_env)) {
            vm_env->wip=REF_create(vm_env->wip);
            vm_ref_hres(vm_env,ENV_LIST(VMRES_DICT),vm_env->wip);
            THROW(!vm_stack_enq(vm_env,REF_ltv(REF_HEAD(vm_env->wip))),LTV_NULL);
            if (exception==REF_ltv(REF_HEAD(vm_env->wip))) {
                LTV_release(vm_deq(vm_env,VMRES_EXC,POP));
                if (!vm_deq(vm_env,VMRES_EXC,KEEP)) // if no remaining exceptions
                    vm_env->state&=~VM_THROWING;
            }
        }
        else { // catch all
            while (exception=vm_deq(vm_env,VMRES_EXC,POP)) // empty remaining exceptions
                LTV_release(exception);
            vm_env->state&=~VM_THROWING;
        }
    }
 done:
    NEXTCALL(vm_env);
}

//////////////////////////////////////////////////

void vmop_TERM_START(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        vm_reset_wip(vm_env);
        NEXTCALL(vm_env);
    }
}

void vmop_REF(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env))
            vm_env->wip=REF_create(vm_env->wip);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_DEREF(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        vm_ref_hres(vm_env,ENV_LIST(VMRES_DICT),vm_use_wip(vm_env));
        LTV *ltv=REF_ltv(REF_HEAD(vm_env->wip));
        if (!ltv)
            ltv=LTV_dup(vm_env->wip);
        THROW(!vm_stack_enq(vm_env,ltv),LTV_NULL);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_PUSHWIP(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env))
            THROW(!vm_stack_enq(vm_env,vm_env->wip),LTV_NULL);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_ITER_POP(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env))
            THROW(!vm_stack_enq(vm_env,vm_env->wip),LTV_NULL);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_ITER_KEEP(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env))
            THROW(!vm_stack_enq(vm_env,vm_env->wip),LTV_NULL);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_ASSIGN(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (!vm_use_wip(vm_env)) { // assign to TOS
            LTV *ltv;
            THROW(!(ltv=vm_stack_deq(vm_env,POP)),LTV_NULL);
            if (ltv->flags&LT_CVAR) { // if TOS is a cvar
                THROW(!vm_stack_enq(vm_env,cif_assign_cvar(ltv,vm_stack_deq(vm_env,POP))),LTV_NULL); // assign directly to it.
                goto done;
            }
            vm_env->wip=REF_create(ltv); // otherwise treat TOS as a reference
        }
        REF_resolve(vm_deq(vm_env,VMRES_DICT,KEEP),vm_env->wip,TRUE);
        THROW(REF_assign(REF_HEAD(vm_env->wip),vm_stack_deq(vm_env,POP)),LTV_NULL);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_REMOVE(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env)) {
            vm_ref_hres(vm_env,ENV_LIST(VMRES_DICT),vm_env->wip);
            REF_remove(REF_HEAD(vm_env->wip));
        } else {
            LTV_release(vm_stack_deq(vm_env,POP));
        }
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_EVAL(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env))
            vm_push_code(vm_env,compile_ltv(compilers[FORMAT_edict],REF_ltv(REF_HEAD(vm_env->wip))));
        else
            vm_push_code(vm_env,compile_ltv(compilers[FORMAT_edict],vm_stack_deq(vm_env,POP)));
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_THROW(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        THROW(vm_use_wip(vm_env),LTV_NULL);
        vm_ref_hres(vm_env,ENV_LIST(VMRES_DICT),vm_env->wip);
        THROW(1,REF_ltv(REF_HEAD(vm_env->wip)));
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_COMPARE(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env)) {
        } else {
        }
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_SPLIT(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        if (vm_use_wip(vm_env)) {
        } else {
        }
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_MERGE(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        LTV *tos,*nos;
        tos=vm_stack_deq(vm_env,POP);
        if (vm_use_wip(vm_env)) {
            vm_ref_hres(vm_env,ENV_LIST(VMRES_DICT),vm_env->wip);
            nos=REF_ltv(REF_HEAD(vm_env->wip));
        } else {
            nos=vm_stack_deq(vm_env,POP);
        }
        vm_stack_enq(vm_env,LTV_concat(tos,nos));
        LTV_release(tos);
        LTV_release(nos);
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_RDLOCK(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state)
        pthread_rwlock_rdlock(&vm_rwlock);
 done:
    NEXTCALL(vm_env);
}

void vmop_WRLOCK(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state)
        pthread_rwlock_wrlock(&vm_rwlock);
 done:
    NEXTCALL(vm_env);
}

void vmop_UNLOCK(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state)
        pthread_rwlock_unlock(&vm_rwlock);
 done:
    NEXTCALL(vm_env);
}

void vmop_FUN_START(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        VM_CMD function_end[] = {{VMOP_CTX_POP},{VMOP_YIELD}};
        if (vm_use_wip(vm_env)) {
            vm_push_code(vm_env,compile(compilers[FORMAT_asm],function_end,(sizeof function_end/sizeof *function_end)));
            vm_push_code(vm_env,compile_ltv(compilers[FORMAT_edict],vm_stack_deq(vm_env,POP)));
            vm_push_code(vm_env,compile_ltv(compilers[FORMAT_edict],vm_env->wip));
            vm_enq(vm_env,VMRES_DICT,LTV_NULL);
            ERROR(!vm_enq(vm_env,VMRES_STACK,LTV_NULL_LIST));
        } else {
        }
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_CTX_START(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
        VM_CMD context_end[] = {{VMOP_CTX_KEEP},{VMOP_YIELD}};
        if (vm_use_wip(vm_env)) {
            vm_push_code(vm_env,compile(compilers[FORMAT_asm],context_end,(sizeof context_end/sizeof *context_end)));
            vm_push_code(vm_env,compile_ltv(compilers[FORMAT_edict],vm_env->wip));
            vm_enq(vm_env,VMRES_DICT,vm_stack_deq(vm_env,POP));
            ERROR(!vm_enq(vm_env,VMRES_STACK,LTV_NULL_LIST));
        } else {
        }
    }
 done:
    NEXTCALL(vm_env);
}

void vmop_BLK_START(VM_ENV *vm_env) { VMOP_DEBUG();
    if (!vm_env->state) {
    }
 done:
    NEXTCALL(vm_env);
}


VMOP_CALL vmop_call[] = {
    vmop_YIELD,
    vmop_EXT,
    vmop_CTX_POP,
    vmop_CTX_KEEP,
    vmop_CATCH,
    vmop_TERM_START,
    vmop_REF,
    vmop_DEREF,
    vmop_PUSHWIP,
    vmop_ITER_POP,
    vmop_ITER_KEEP,
    vmop_ASSIGN,
    vmop_REMOVE,
    vmop_EVAL,
    vmop_THROW,
    vmop_COMPARE,
    vmop_SPLIT,
    vmop_MERGE,
    vmop_RDLOCK,
    vmop_WRLOCK,
    vmop_UNLOCK,
    vmop_FUN_START,
    vmop_CTX_START,
    vmop_BLK_START
};

//////////////////////////////////////////////////
// Opcode Dispatch

int vm_eval(VM_ENV *vm_env)
{
    while (!(vm_env->state&(VM_COMPLETE|VM_ERROR))) {
        vm_get_code(vm_env);
        if (vm_env->code_ltv->flags&LT_CVAR) { // type, ffi, ...
            vm_eval_type(vm_env,vm_env->code_ltv);
            vm_pop_code(vm_env);
        } else {
            DEBUG(fprintf(stderr,"start ------------------------\n"));
            VMOP_DEBUG();
            vm_reset_wip(vm_env);
            VMOP_CALL call=vmop_call[OPCODE(++)];
            call(vm_env);
            vm_env->state&=~VM_BYPASS;
        }
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
    VM_ENV *vm_env=env_cvar->data;
    LTV *ffi_type_info=LT_get(env_cvar,"FFI_CIF_LTV",HEAD,KEEP);

    LTV *locals=LTV_init(NEW(LTV),"THUNK_LOCALS",-1,LT_NONE);
    STRY(!vm_enq(vm_env,VMRES_DICT,locals),"pushing locals into dict");

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

    vm_eval(vm_env);

 done:
    return;
}


extern void *vm_create_cb(char *callback_type,LTV *root,LTV *code)
{
    int status=0;
    LTV *ffi_cif_ltv=cif_find_function(cif_type_info(callback_type));
    STRY(!ffi_cif_ltv,"validating callback type");

    LTV *env_cvar=cif_create_cvar(cif_type_info("VM_ENV"),NEW(VM_ENV),NULL);
    STRY(!env_cvar,"validating creation of env cvar");

    VM_ENV *vm_env=env_cvar->data; // finesse: address of LTV array is also the address of the first element of the array.

    for (int i=0;i<VMRES_COUNT;i++)
        LTV_init(&vm_env->ltv[i],NULL,0,LT_NULL|LT_LIST);
    vm_env->state=0;

    LTV *dict_anchor=LTV_NULL;
    LT_put(dict_anchor,"ENV",HEAD,env_cvar);

    vm_push_code(vm_env,code); // ,"pushing code");
    STRY(!vm_enq(vm_env,VMRES_DICT,dict_anchor),"adding anchor to dict");
    STRY(!vm_enq(vm_env,VMRES_DICT,root),"addning reflection to dict");
    STRY(!vm_enq(vm_env,VMRES_STACK,LTV_NULL_LIST),"initializing stack");

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
        "[bootstrap.edict] [r] file_open! @bootstrap\n"
        "[bootstrap brl! ! bootloop!]@bootloop\n"
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
