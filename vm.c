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

enum {
    VMRES_DICT,
    VMRES_CODE,
    VMRES_STACK,
    VMRES_EXC,
    VMRES_COUNT,
};

enum {
    VM_BYPASS   = 0x01,
    VM_THROWING = 0x02,
    VM_YIELD    = 0x04,
    VM_COMPLETE = 0x08,
    VM_ERROR    = 0x10,
    VM_HALT     = VM_YIELD | VM_COMPLETE | VM_ERROR
};

pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

char *res_name[] = { "dict","code","stack","exc" };

typedef struct {
    LTV ltv[VMRES_COUNT];
    int state;
    unsigned ext_length;
    unsigned ext_flags;
    char *ext_data;
    LTV *wip;
} VM_ENV;

#define ENV_LIST(res) LTV_list(&vm_env->ltv[res])

typedef void *(*VMOP_CALL)();

LTV *env_enq(VM_ENV *vm_env,int res,LTV *tos) { return LTV_put(ENV_LIST(res),tos,HEAD,NULL); }
LTV *env_deq(VM_ENV *vm_env,int res,int pop)  { return LTV_get(ENV_LIST(res),pop,HEAD,NULL,NULL); }

//////////////////////////////////////////////////
// Bytecode Interpreter
//////////////////////////////////////////////////
int vm_eval(VM_ENV *vm_env)
{
    int status=0;

    static VMOP_CALL vmop_call[VMOP_COUNT];
    LTV *code_ltv=NULL,*opcode_ltv=NULL;
    LTVR *code_ltvr=NULL;

#define OPCODE(OP) (*(char *) (opcode_ltv->data OP))
#define NEXTCALL vmop_call[OPCODE(++)]

    LTV *enq(int res,LTV *ltv) { return LTV_put(ENV_LIST(res),ltv,HEAD,NULL); }
    LTV *deq(int res,int pop) { return LTV_get(ENV_LIST(res),pop,HEAD,NULL,NULL); }

    LTV *push_code(LTV *ltv) {
        DEBUG(printf(CODE_RED "  ENQ CODE %x" CODE_RESET "\n",ltv));
        return enq(VMRES_CODE,ltv);
    }
    LTV *get_code() { return LTV_get(ENV_LIST(VMRES_CODE),KEEP,HEAD,NULL,&code_ltvr); }
    void release_code() {
        if (code_ltvr) {
            DEBUG(printf(CODE_RED "  DEQ CODE %x" CODE_RESET "\n",code_ltvr->ltv));
            LTVR_release(&code_ltvr->lnk);
        }
    }

    LTV *stack_enq(LTV *ltv) { return LTV_enq(LTV_list(deq(VMRES_STACK,KEEP)),ltv,HEAD); }
    LTV *stack_deq(int pop) {
        void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
        LTV *rval=CLL_map(ENV_LIST(VMRES_STACK),FWD,op);
        return rval;
    }

    int concat(int res) { // merge tos and nos LTVs
        int status=0;
        LTV *tos,*nos,*ltv;
        STRY(!(tos=deq(res,POP)),"popping wip");
        STRY(!(nos=deq(res,POP)),"peeking wip");
        STRY(!(ltv=LTV_concat(tos,nos)),"concatenating tos and ns");
        LTV_release(tos);
        LTV_release(nos);
        enq(res,ltv);
    done:
        return status;
    }

    int listcat(int res) { // merge tos into head of nos
        int status=0;
        LTV *tos,*nos;
        STRY(!(tos=deq(res,POP)),"popping res %d",res);
        STRY(!(nos=deq(res,KEEP)),"peeking res %d",res);
        STRY(!(tos->flags&LT_LIST && nos->flags&LT_LIST),"verifying lists");
        CLL_MERGE(LTV_list(nos),LTV_list(tos),HEAD);
        LTV_release(tos);
    done:
        return status;
    }

    int ref_hres(CLL *cll,LTV *ref) {
        int status=0;
        LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
        void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
        CLL_map(cll,FWD,op);
    done:
        return status;
    }

    int dump(LTV *ltv,char *label) {
        int status=0;
        char *filename;
        printf("%s\n",label);
        print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0);
        graph_ltv_to_file(FORMATA(filename,32,"/tmp/%s.dot",label),ltv,0,label);
    done:
        return status;
    }

    int ffi(LTV *lambda) { // adapted from edict.c's ffi_eval(...)
        int status=0;
        CLL args; CLL_init(&args); // list of ffi arguments
        LTV *rval=NULL;
        STRY(!(rval=cif_rval_create(lambda,NULL)),"creating ffi rval ltv");
        int marshaller(char *name,LTV *type) {
            int status=0;
            LTV *arg=NULL, *coerced=NULL;
            STRY(!(arg=stack_deq(POP)),"popping ffi arg (%s) from stack",name); // FIXME: attempt to resolve by name first
            STRY(!(coerced=cif_coerce(arg,type)),"coercing ffi arg");
            LTV_enq(&args,coerced,HEAD); // enq coerced arg onto args CLL
            LT_put(rval,name,HEAD,coerced); // coerced args are installed as childen of rval
            LTV_release(arg);
        done:
            return status;
        }
        STRY(cif_args_marshal(lambda,marshaller),"marshalling ffi args"); // pre-
        STRY(cif_ffi_call(lambda,rval,&args),"calling ffi");
        CLL_release(&args,LTVR_release);
        STRY(!stack_enq(rval),"enqueing rval onto stack");
    done:
        return status;
    }

    int cvar(LTV *type) {
        int status=0;
        LTV *cvar;
        STRY(!type,"validating type");
        STRY(!(cvar=cif_create_cvar(type,NULL,NULL)),"creating cvar");
        STRY(!stack_enq(cvar),"pushing cvar");
    done:
        return status;
    }

    int eval_type(LTV *type) {
        if (LT_get(type,FFI_CIF,HEAD,KEEP))
            return ffi(type);
        else
            return cvar(type);
    }

    int throw(LTV *ltv) {
        int status=0;
        STRY(!enq(VMRES_EXC,ltv),"Throwing");
        vm_env->state|=VM_THROWING;
    done:
        return status;
    }

    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
    if (!(code_ltv=get_code())) {
        status|=VM_COMPLETE;
        goto done;
    } else if (code_ltv->flags&LT_TYPE) { // type, ffi, ...
        if (eval_type(code_ltv))
            throw(code_ltv);
        release_code();
        goto done;
    } /// else ...

    opcode_ltv=LT_get(code_ltv,"opcode",HEAD,KEEP);
    if (!opcode_ltv) {
        STRY(!(opcode_ltv=LTV_init(NEW(LTV),code_ltv->data,1,LT_NONE)),"creating opcode tracker");
        STRY(!LT_put(code_ltv,"opcode",HEAD,opcode_ltv),"inserting opcode tracker");
    }

    void reset_wip() {
        vm_env->ext_length=vm_env->ext_flags=0;
        vm_env->ext_data=NULL;
        if (vm_env->wip)
            LTV_release(vm_env->wip);
        vm_env->wip=NULL;
    }

    LTV *use_wip() {
        if (vm_env->ext_data && !vm_env->wip)
            vm_env->wip=LTV_init(NEW(LTV),vm_env->ext_data,vm_env->ext_length,vm_env->ext_flags);
        DEBUG(print_ltv(stdout,CODE_RED "use wip: ",vm_env->wip,CODE_RESET "\n",0));
        return vm_env->wip;
    }

    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
    //////////////////////////////////////////////////

#define VMOP_DEBUG() DEBUG(printf("%s (code %x loc %x nextop %x state %x)\n",__func__,code_ltv,opcode_ltv->data,OPCODE(),vm_env->state));


    void *vmop_TERM_START() { VMOP_DEBUG();
        reset_wip();
        return NEXTCALL;
    }

#define THROW(expression) do { if (expression) { throw(LTV_NULL); reset_wip(); goto done; } } while(0)

    void *vmop_EXT() { DEBUG(printf("%s\n ",__func__));
        vm_env->ext_length=ntohl(*(unsigned *) opcode_ltv->data); opcode_ltv->data+=sizeof(unsigned);
        vm_env->ext_flags=ntohl(*(unsigned *)  opcode_ltv->data); opcode_ltv->data+=sizeof(unsigned);
        vm_env->ext_data=opcode_ltv->data;                        opcode_ltv->data+=vm_env->ext_length;
        DEBUG(printf(CODE_BLUE " "); printf("len %d flags %x state %x;",vm_env->ext_length,vm_env->ext_flags,vm_env->state); fstrnprint(stdout,vm_env->ext_data,vm_env->ext_length); printf(CODE_RESET "\n"));
        return NEXTCALL;
    }

    void *vmop_CODE_END() { VMOP_DEBUG();
        release_code();
        vm_env->state|=VM_YIELD;
        return NULL;
    }

    void *vmop_CTX_POP() { VMOP_DEBUG();
        int status=0;
        STRY(listcat(VMRES_STACK),"collapsing stack levels");
        LTV_release(deq(VMRES_DICT,POP));
    done:
        return vmop_CODE_END(); // implicit
    }

    void *vmop_CTX_KEEP() { VMOP_DEBUG();
        int status=0;
        STRY(listcat(VMRES_STACK),"collapsing stack levels");
        STRY(!stack_enq(deq(VMRES_DICT,POP)),"returning dict level to stack");
    done:
        return vmop_CODE_END(); // implicit
    }

    void *vmop_CATCH() { VMOP_DEBUG();
        int status=0;
        if (!vm_env->state)
            vm_env->state|=VM_BYPASS;
        else if (vm_env->state|VM_THROWING) {
            LTV *exception=NULL;
            STRY(!(exception=deq(VMRES_EXC,KEEP)),"peeking at exception stack");
            if (use_wip()) {
                vm_env->wip=REF_create(vm_env->wip);
                ref_hres(ENV_LIST(VMRES_DICT),vm_env->wip);
                THROW(!stack_enq(REF_ltv(REF_HEAD(vm_env->wip))));
                if (exception==REF_ltv(REF_HEAD(vm_env->wip))) {
                    LTV_release(deq(VMRES_EXC,POP));
                    if (!deq(VMRES_EXC,KEEP)) // if no remaining exceptions
                        vm_env->state&=~VM_THROWING;
                }
            }
            else { // catch all
                while (exception=deq(VMRES_EXC,POP)) // empty remaining exceptions
                    LTV_release(exception);
                vm_env->state&=~VM_THROWING;
            }
        }
    done:
        return NEXTCALL;
    }

    void *vmop_BUILTIN() { VMOP_DEBUG();
        int match(char *key) { return !strncmp(key,vm_env->ext_data,vm_env->ext_length); }
        LTV *cvar_ltv=NULL,*ptr=NULL;

        if (match("dump")) {
            dump(&vm_env->ltv[VMRES_DICT],res_name[VMRES_DICT]);
            dump(&vm_env->ltv[VMRES_STACK],res_name[VMRES_STACK]);
        } else if (match("import")) {
            LTV *mod=NULL;
            STRY(cif_curate_module(stack_deq(KEEP),false),"importing module");
        } else if (match("locals")) {
            int old_show_ref=show_ref;
            show_ref=1;
            dump(deq(VMRES_DICT,KEEP),res_name[VMRES_DICT]);
            dump(deq(VMRES_STACK,KEEP),res_name[VMRES_STACK]);
            show_ref=old_show_ref;
        } else if (match("hoist")) { // ltv -> cvar(ltv)
            LTV *ltv=NULL;
            THROW(!(ltv=stack_deq(POP))); // ,"popping ltv to hoist");
            THROW(!(stack_enq(cif_create_cvar(cif_type_info("(LTV)*"),ltv,NULL)))); // ,"pushing hoisted ltv cvar");
        } else if (match("plop")) { // cvar(ltv) -> ltv
            THROW(!(cvar_ltv=stack_deq(POP)));
            THROW(!(cvar_ltv->flags&LT_CVAR)); // "checking at least if it's a cvar" // TODO: verify it's an "(LTV)*"
            THROW(!(ptr=*(LTV **) (cvar_ltv->data)));
            THROW(!(stack_enq(ptr)));
        }
    done:
        LTV_release(cvar_ltv);
        return NEXTCALL;
    }

    void *vmop_PUSHWIP() { VMOP_DEBUG();
        if (use_wip())
            THROW(!stack_enq(vm_env->wip));
        else {
        }
    done:
        return NEXTCALL;
    }

    void *vmop_REF() { VMOP_DEBUG();
        if (use_wip())
            vm_env->wip=REF_create(vm_env->wip);
        return NEXTCALL;
    }

    void *vmop_DEREF() { VMOP_DEBUG();
        ref_hres(ENV_LIST(VMRES_DICT),use_wip());
        LTV *ltv=REF_ltv(REF_HEAD(vm_env->wip));
        if (!ltv)
            ltv=LTV_dup(vm_env->wip);
        THROW(!stack_enq(ltv));
    done:
        return NEXTCALL;
    }

    void *vmop_ASSIGN() { VMOP_DEBUG();
        if (!use_wip())
            vm_env->wip=REF_create(stack_deq(POP));
        REF_resolve(deq(VMRES_DICT,KEEP),vm_env->wip,TRUE);
        THROW(REF_assign(REF_HEAD(vm_env->wip),stack_deq(POP)));
    done:
        return NEXTCALL;
    }

    void *vmop_REMOVE() { VMOP_DEBUG();
        if (use_wip()) {
            ref_hres(ENV_LIST(VMRES_DICT),vm_env->wip);
            REF_remove(REF_HEAD(vm_env->wip));
        } else {
            LTV_release(stack_deq(POP));
        }
        return NEXTCALL;
    }

    void *vmop_EVAL() { VMOP_DEBUG();
        if (use_wip())
            push_code(compile_ltv(compilers[FORMAT_edict],REF_ltv(REF_HEAD(vm_env->wip))));
        else
            push_code(compile_ltv(compilers[FORMAT_edict],stack_deq(POP)));
        if (OPCODE()==VMOP_CODE_END)
            release_code();
        vm_env->state|=VM_YIELD;
        return NULL;
    }

    void *vmop_THROW() { VMOP_DEBUG();
        if (use_wip()) {
            ref_hres(ENV_LIST(VMRES_DICT),vm_env->wip);
            throw(REF_ltv(REF_HEAD(vm_env->wip)));
        } else {
            throw(LTV_NULL);
        }
        return NEXTCALL;
    }

    void *vmop_COMPARE() { VMOP_DEBUG();
        if (use_wip()) {
        } else {
        }
        return NEXTCALL;
    }

    void *vmop_SPLIT() { VMOP_DEBUG();
        return NEXTCALL;
    }

    void *vmop_MERGE() { VMOP_DEBUG();
        LTV *tos,*nos;
        tos=stack_deq(POP);
        if (use_wip()) {
            ref_hres(ENV_LIST(VMRES_DICT),vm_env->wip);
            nos=REF_ltv(REF_HEAD(vm_env->wip));
        } else {
            nos=stack_deq(POP);
        }
        stack_enq(LTV_concat(tos,nos));
        LTV_release(tos);
        LTV_release(nos);
        return NEXTCALL;
    }

    void *vmop_RDLOCK() { VMOP_DEBUG();
        pthread_rwlock_rdlock(&vm_rwlock);
        return NEXTCALL;
    }

    void *vmop_WRLOCK() { VMOP_DEBUG();
        pthread_rwlock_wrlock(&vm_rwlock);
        return NEXTCALL;
    }

    void *vmop_UNLOCK() { VMOP_DEBUG();
        pthread_rwlock_unlock(&vm_rwlock);
        return NEXTCALL;
    }

    void *vmop_FUN_START() { VMOP_DEBUG();
        VM_CMD function_end[] = {{VMOP_CTX_KEEP},{VMOP_CODE_END}};
        if (use_wip()) {
            push_code(compile(compilers[FORMAT_asm],function_end,sizeof(function_end)));
            push_code(compile_ltv(compilers[FORMAT_edict],stack_deq(POP)));
            push_code(compile_ltv(compilers[FORMAT_edict],vm_env->wip));
            enq(VMRES_DICT,LTV_NULL);
            STRY(!enq(VMRES_STACK,LTV_NULL_LIST),"pushing stack level");
        } else {
        }
        if (OPCODE()==VMOP_CODE_END)
            release_code();
        vm_env->state|=VM_YIELD;
    done:
        return NEXTCALL;
    }

    void *vmop_CTX_START() { VMOP_DEBUG();
        VM_CMD context_end[] = {{VMOP_CTX_KEEP},{VMOP_CODE_END}};
        if (use_wip()) {
            push_code(compile(compilers[FORMAT_asm],context_end,sizeof(context_end)));
            push_code(compile_ltv(compilers[FORMAT_edict],vm_env->wip));
            enq(VMRES_DICT,stack_deq(POP));
            STRY(!enq(VMRES_STACK,LTV_NULL_LIST),"pushing stack level");
        } else {
        }
        if (OPCODE()==VMOP_CODE_END)
            release_code();
        vm_env->state|=VM_YIELD;
    done:
        return NEXTCALL;
    }

    void *vmop_BLK_START() { VMOP_DEBUG();
        if (use_wip()) {
        } else {
        }
        return NEXTCALL;
    }

    static int init=0;
    if (!init) {
        vmop_call[init++] = vmop_CODE_END;
        vmop_call[init++] = vmop_EXT;
        vmop_call[init++] = vmop_CTX_POP;
        vmop_call[init++] = vmop_CTX_KEEP;
        vmop_call[init++] = vmop_CATCH;
        vmop_call[init++] = vmop_TERM_START;
        vmop_call[init++] = vmop_BUILTIN;
        vmop_call[init++] = vmop_PUSHWIP;
        vmop_call[init++] = vmop_REF;
        vmop_call[init++] = vmop_DEREF;
        vmop_call[init++] = vmop_ASSIGN;
        vmop_call[init++] = vmop_REMOVE;
        vmop_call[init++] = vmop_EVAL;
        vmop_call[init++] = vmop_THROW;
        vmop_call[init++] = vmop_COMPARE;
        vmop_call[init++] = vmop_SPLIT;
        vmop_call[init++] = vmop_MERGE;
        vmop_call[init++] = vmop_RDLOCK;
        vmop_call[init++] = vmop_WRLOCK;
        vmop_call[init++] = vmop_UNLOCK;
        vmop_call[init++] = vmop_FUN_START;
        vmop_call[init++] = vmop_CTX_START;
        vmop_call[init++] = vmop_BLK_START;
    };

    void *bypass() { VMOP_DEBUG();
        int opcode;
        for (opcode=OPCODE();opcode>VMOP_CATCH;opcode=OPCODE(++)) {
            DEBUG(printf("bypass check %x\n",OPCODE()));
        }
        DEBUG(printf(CODE_RED "after skip, returning vmop for opcode %x" CODE_RESET "\n",opcode));
        VMOP_DEBUG();
        return vmop_call[opcode];
    }

    VMOP_DEBUG();

    reset_wip();
    VMOP_CALL call=vmop_call[OPCODE()];
    while (!(vm_env->state&VM_HALT)) {
        if (vm_env->state)
            call=bypass();
        call=call();
    }

    DEBUG(printf("exit ------------------------\n"));

 done:
    vm_env->state&=~(VM_BYPASS|VM_YIELD);
    return status;
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

    LTV *locals=LTV_init(NEW(LTV),"BOOTSTRAP_LOCALS",-1,LT_NONE);
    STRY(!env_enq(vm_env,VMRES_DICT,locals),"pushing locals into dict");

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

    while (!(vm_eval(vm_env)&(VM_COMPLETE|VM_ERROR)));

 done:
    return;
}


extern void *vm_create_env(char *callback_type,LTV *root,LTV *code)
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

    STRY(!env_enq(vm_env,VMRES_CODE,code),"pushing code");
    STRY(!env_enq(vm_env,VMRES_DICT,env_cvar),"adding vm_env to dict");
    STRY(!env_enq(vm_env,VMRES_DICT,root),"addning reflection to dict");
    STRY(!env_enq(vm_env,VMRES_STACK,LTV_NULL_LIST),"initializing stack");

    STRY(cif_cb_create(ffi_cif_ltv,vm_cb_thunk,env_cvar),"creating vm environment/callback");
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
        "[bootstrap brl! `plop`! bootloop!]@bootloop\n"
        "bootloop() | 0@RETURN\n";
    LTV *code=compile(compilers[FORMAT_edict],bootstrap_code,strlen(bootstrap_code));
    vm_bootstrap bootstrap=(vm_bootstrap) vm_create_env("vm_bootstrap",cif_module,code);
    try_depth=0;
    try_loglev=0;
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
