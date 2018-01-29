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

sem_t vm_process_access,vm_process_escapement;
pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

__attribute__((constructor))
static void init(void)
{
    sem_init(&vm_process_access,1,0); // allows first wait through
    sem_init(&vm_process_escapement,0,0); // blocks on first wait
}

char *res_name[] = { "dict","code","wip","stack","exc","state","skip" };

LTV *env_enq(LTV **res_ltv,int res,LTV *tos) { return LTV_put(LTV_list(res_ltv[res]),tos,HEAD,NULL); }
LTV *env_deq(LTV **res_ltv,int res,int pop) { return LTV_get(LTV_list(res_ltv[res]),pop,HEAD,NULL,NULL); }

//////////////////////////////////////////////////
// Bytecode Interpreter
//////////////////////////////////////////////////
int vm_eval(LTV *env_cvar)
{
    int status=0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    LTV **res_ltv=(LTV **) env_cvar->data;
    CLL *res_cll[VMRES_CLL_COUNT];

    for (int i=0;i<VMRES_CLL_COUNT;i++)
        STRY(!(res_cll[i]=LTV_list(res_ltv[i])),"caching lists");
    int *env_state=(int *) res_ltv[VMRES_STATE]->data;
    int *env_skip =(int *) res_ltv[VMRES_SKIP]->data;

    unsigned char imm=0;
    LTV *ref=NULL;
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    LTV *enq(int res,LTV *ltv) { return LTV_put(res_cll[res],ltv,HEAD,NULL); }
    LTV *deq(int res,int pop) { return LTV_get(res_cll[res],pop,HEAD,NULL,NULL); }

    LTV *stack_enq(LTV *ltv) { return LTV_enq(LTV_list(deq(VMRES_STACK,KEEP)),ltv,HEAD); }
    LTV *stack_deq(int pop) {
        void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
        return CLL_map(res_cll[VMRES_STACK],FWD,op);
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
        STRY(!(tos=deq(res,POP)),"popping wip");
        STRY(!(nos=deq(res,KEEP)),"peeking wip");
        STRY(!(tos->flags&LT_LIST && nos->flags&LT_LIST),"verifying lists");
        CLL_MERGE(LTV_list(nos),LTV_list(tos),HEAD);
        LTV_release(tos);
    done:
        return status;
    }

    int context_push() {
        int status=0;
        STRY(!enq(VMRES_DICT,deq(VMRES_WIP,POP)),"pushing dict level");
        STRY(!enq(VMRES_STACK,LTV_NULL_LIST),"pushing stack level");
    done:
        return status;
    }

    int context_pop() {
        int status=0;
        STRY(listcat(VMRES_STACK),"collapsing stack levels");
        STRY(!stack_enq(deq(VMRES_DICT,POP)),"returning dict level to stack");
    done:
        return status;
    }

    int dump() {
        int status=0;
        print_ltv(stdout,CODE_RED,res_ltv[VMRES_DICT],CODE_RESET "\n",0);
        graph_ltv_to_file("/tmp/env.dot",res_ltv[VMRES_DICT],0,NULL);
        return status;
    }

    int ref_hres(CLL *cll,LTV *ref) {
        int status=0;
        LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
        void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
        STRY(!CLL_map(cll,FWD,op),"resolving ref");
    done:
        return status;
    }

    int dumpstack() {
        int status=0;
        print_ltvs(stdout,CODE_RED,res_cll[VMRES_STACK],CODE_RESET "\n",0);
        graph_ltvs_to_file("/tmp/vm_tos.dot",res_cll[VMRES_STACK],0,"TOS");
    done:
        return status;
    }

    int ffi(LTV *lambda) { // adapted from edict.c's ffi_eval(...)
        int status=0;
        CLL args; CLL_init(&args); // list of ffi arguments
        LTV *rval=NULL;
        STRY(!(rval=cif_rval_create(lambda)),"creating ffi rval ltv");
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
        (*env_state)|=VM_THROWING;
    done:
        return status;
    }

    // translate to vm-based implementation
    int catch() {
        int status=0;
        LTV *exception=NULL;
        STRY(!(exception=deq(VMRES_EXC,KEEP)),"peeking at exception stack");
        if (!ref) { // catch all
            while (exception=deq(VMRES_EXC,POP))
                LTV_release(exception);
            (*env_state)&=~VM_THROWING;
        }
        else if (exception==REF_ltv(REF_HEAD(ref))) { // specific exception
            LTV_release(deq(VMRES_EXC,POP));
            if (!deq(VMRES_EXC,KEEP))
                (*env_state)&=~VM_THROWING;
        }
    done:
        return status;
    }

    void skip() {
        (*env_skip)++;
        (*env_state)|=VM_SKIPPING;
    }

    void unskip() {
        if ((*env_skip))
            (*env_skip)--;
        if (!(*env_skip))
            (*env_state)&=~VM_SKIPPING;
    }

    int exc_deframe()
    {
        int status=0;
        if      ((*env_state)|VM_SKIPPING) unskip();
        else if ((*env_state)|VM_THROWING) STRY(context_pop(),"popping context");
        else if ((*env_state)|VM_BYPASS)   { STRY(context_pop(),"popping context"); (*env_state)&=~VM_BYPASS; }
    done:
        return status;
    }

    int builtin() {
        int status=0;
        LTV *tmp=NULL;
        if (!strncmp("dump",ref->data,ref->len))
            dump();
        else if (!strncmp("import",ref->data,ref->len)) {
            LTV *mod=NULL;
            STRY(!(mod=stack_deq(KEEP)),"getting module name");
            STRY(cif_curate_module(mod,false),"importing module");
        }
        else if (!strncmp("stack",ref->data,ref->len)) {
            int old_show_ref=show_ref;
            show_ref=1;
            dumpstack();
            graph_ltvs_to_file("/tmp/stack.dot",res_cll[VMRES_STACK],0,res_name[VMRES_STACK]);
            show_ref=old_show_ref;
        }
        else if (!strncmp("hoist",ref->data,ref->len)) { // ltv -> cvar(ltv)
            LTV *ltv=NULL;
            STRY(!(ltv=deq(VMRES_WIP,POP)),"popping ltv to hoist");
            STRY(!(enq(VMRES_WIP,cif_create_cvar(cif_type_info("(LTV)*"),NULL,NULL))),"pushing hoisted ltv cvar");
        }
        else if (!strncmp("drop",ref->data,ref->len)) { // cvar(ltv) -> ltv
            LTV *cvar_ltv=NULL;
            STRY(!(cvar_ltv=deq(VMRES_WIP,POP)),"popping ltv cvar to drop");
            STRY(!(cvar_ltv->flags&LT_CVAR),"checking at least if it's a cvar"); // TODO: verify it's an "(LTV)*"
            STRY(!(enq(VMRES_WIP,*(LTV **) cvar_ltv->data)),"pushing dropped ltv");
            LTV_release(cvar_ltv);
        }

    done:
        return status;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    LTV *code_ltv=NULL,*ip_ltv=NULL;
    LTVR *code_ltvr=NULL;

    LTV *get_code() { return LTV_get(res_cll[VMRES_CODE],KEEP,HEAD,NULL,&code_ltvr); }
    void release_code() { if (code_ltvr) LTVR_release(&code_ltvr->lnk); }

    if (!(code_ltv=get_code())) {
        status=!0;
        goto done;
    }

    if (code_ltv->flags&LT_TYPE) {
        STRY(eval_type(code_ltv),"executing ffi");
        release_code();
    } else {
        char *data=(char *) code_ltv->data;
        int len=code_ltv->len;
        ip_ltv=LT_get(code_ltv,"ip",HEAD,KEEP);
        if (!ip_ltv)
            STRY(!(ip_ltv=LT_put(code_ltv,"ip",HEAD,LTV_ZERO)),"inserting CODE IP");
        int *ip=(int *) &ip_ltv->data;

        LTV *decode_extended() {
            unsigned length=ntohl(*(unsigned *) (data+(*ip)));   (*ip)+=sizeof(unsigned);
            unsigned flags=ntohl(*(unsigned *) (data+(*ip)));    (*ip)+=sizeof(unsigned);
            LTV *ltv=LTV_init(NEW(LTV),data+(*ip),length,flags); (*ip)+=length;
            return ltv;
        }

        int map_make(int pop) {
            int status=0;
            STRY(!REF_ltv(REF_HEAD(ref)),"validating ref head");
            LTV *map=NULL,*lambda=NULL;
            VM_CMD map_bytecodes[] = {{pop?VMOP_MAP_POP:VMOP_MAP_KEEP}};
            STRY(!(lambda=compile_ltv(compilers[FORMAT_edict],deq(VMRES_WIP,POP))),"compiling lambda for map wrapper");
            STRY(!(map=compile(compilers[FORMAT_asm],map_bytecodes,1)),"creating map wrapper");
            LT_put(map,"lambda",HEAD,lambda); // install lambda
            LT_put(map,"ref",HEAD,ref); // install_ref
            STRY(!env_enq(res_ltv,VMRES_CODE,map),"pushing map");
        done:
            return status;
        }

        int map(int pop) {
            int status=0;
            STRY(!(ref=LT_get(code_ltv,"ref",HEAD,KEEP)),"retrieving ref from map wrapper");;
            LTV *lambda=NULL,*ltv=NULL;
            STRY(!(lambda=LT_get(code_ltv,"lambda",HEAD,KEEP)),"retrieving lambda from map wrapper");;
            STRY(!(ltv=REF_ltv(REF_HEAD(ref))),"dereferencing ref from map wrapper");
            stack_enq(ltv);
            if (!REF_iterate(ref,pop) && REF_ltv(REF_HEAD(ref))) // will there be a next round?
                STRY(!env_enq(res_ltv,VMRES_CODE,code_ltv),"pushing code_ltv"); // if so, requeue wrapper
            STRY(!env_enq(res_ltv,VMRES_CODE,lambda),"pushing lambda");
        done:
            return status;
        }

        //#define OPCODE(vmop) case (unsigned char) vmop: printf(CODE_RED "0x%x\n" CODE_RESET,(unsigned char) vmop);
#define OPCODE(vmop) case (unsigned char) vmop:

        while ((*ip)<len) {
            unsigned char op=data[(*ip)++];
            if (*env_state) {
                switch(op) {
                    OPCODE(VMOP_REF)      TRYCATCH(!(ref=REF_create(decode_extended())),op,bc_exc,"decoding a ref"); continue;
                    OPCODE(VMOP_REF_ERES) TRYCATCH(ref_hres(res_cll[VMRES_DICT],ref),op,bc_exc,"hierarchically resolving ref"); continue;
                    OPCODE(VMOP_REF_KILL) LTV_release(ref); ref=NULL; continue;
                    OPCODE(VMOP_ENFRAME)  skip(); continue;
                    OPCODE(VMOP_DEFRAME)  TRYCATCH(exc_deframe(),op,bc_exc,"deframing in exception"); continue;
                    OPCODE(VMOP_CATCH)    if ((*env_state)|VM_THROWING) catch(); continue;
                    default: continue;
                }
            } else {
                switch(op) {
                    //////////////////////////////////////////////////////
                    case 0xff-VMRES_DICT:  imm=VMRES_DICT;  continue;
                    case 0xff-VMRES_CODE:  imm=VMRES_CODE;  continue;
                    case 0xff-VMRES_WIP:   imm=VMRES_WIP;   continue;
                    case 0xff-VMRES_STACK: imm=VMRES_STACK; continue;
                    case 0xff-VMRES_EXC:   imm=VMRES_EXC;   continue;
                    //////////////////////////////////////////////////////

                    OPCODE(VMOP_REF)       TRYCATCH(!(ref=REF_create(decode_extended())),op,bc_exc,"decoding a ref"); continue;
                    OPCODE(VMOP_REF_ERES)  continue; // unneeded when not in exception mode
                    OPCODE(VMOP_REF_HRES)  TRYCATCH(ref_hres(res_cll[VMRES_DICT],ref),op,bc_exc,"hierarchically resolving ref"); continue;
                    OPCODE(VMOP_REF_KILL)  LTV_release(ref); ref=NULL; continue;
                    OPCODE(VMOP_ENFRAME)   TRYCATCH(context_push(),op,bc_exc,"pushing context"); continue;
                    OPCODE(VMOP_DEFRAME)   TRYCATCH(context_pop(),op,bc_exc,"popping context");  continue;
                    OPCODE(VMOP_CATCH)     (*env_state)|=VM_BYPASS; continue;

                    OPCODE(VMOP_THROW)     throw(REF_ltv(REF_HEAD(ref))); continue;

                    OPCODE(VMOP_NULL_ITEM) TRYCATCH(!enq(VMRES_WIP,LTV_NULL),op,bc_exc,"pushing NULL to WIP");  continue;
                    OPCODE(VMOP_NULL_LIST) TRYCATCH(!enq(VMRES_WIP,LTV_NULL_LIST),op,bc_exc,"pushing NULL LIST to WIP");  continue;

                    OPCODE(VMOP_SPUSH)     TRYCATCH(!stack_enq(deq(VMRES_WIP,POP)),op,bc_exc,"SPUSH'ing");  continue;
                    OPCODE(VMOP_SPOP)      TRYCATCH(!enq(VMRES_WIP,stack_deq(POP)),op,bc_exc,"SPOP'ing");   continue;
                    OPCODE(VMOP_SPEEK)     TRYCATCH(!enq(VMRES_WIP,stack_deq(KEEP)),op,bc_exc,"SPEEK'ing"); continue;

                    OPCODE(VMOP_PUSH)      enq(imm,deq(VMRES_WIP,POP));  continue;
                    OPCODE(VMOP_POP)       enq(VMRES_WIP,deq(imm,POP));  continue;
                    OPCODE(VMOP_PEEK)      enq(VMRES_WIP,deq(imm,KEEP)); continue;
                    OPCODE(VMOP_DUP)       enq(imm,deq(imm,KEEP));       continue;
                    OPCODE(VMOP_DROP)      LTV_release(deq(imm,POP));    continue;

                    OPCODE(VMOP_CONCAT)    TRYCATCH(concat(imm),op,bc_exc,"concatting two LTVs"); continue;
                    OPCODE(VMOP_LISTCAT)   TRYCATCH(listcat(imm),op,bc_exc,"concatting two LTV lists"); continue;

                    OPCODE(VMOP_LIT)       enq(VMRES_WIP,decode_extended()); continue;
                    OPCODE(VMOP_BUILTIN)   builtin();   continue;
                    OPCODE(VMOP_TOS)       dumpstack(); continue;

                    OPCODE(VMOP_REF_MAKE)  TRYCATCH(!(ref=REF_create(deq(VMRES_WIP,POP))),op,bc_exc,"making a ref");            continue;
                    OPCODE(VMOP_REF_INS)   TRYCATCH(REF_resolve(deq(VMRES_DICT,KEEP),ref,TRUE),op,bc_exc,"inserting ref");      continue;
                    OPCODE(VMOP_REF_RES)   TRYCATCH(REF_resolve(deq(VMRES_DICT,KEEP),ref,FALSE),op,bc_exc,"resolving ref");     continue;
                    OPCODE(VMOP_REF_ITER)  TRYCATCH(REF_iterate(ref,KEEP),op,bc_exc,"iterating ref");                           continue;
                    OPCODE(VMOP_DEREF)     TRYCATCH(!enq(VMRES_WIP,REF_ltv(REF_HEAD(ref))),op,bc_exc,"dereferencing");          continue;
                    OPCODE(VMOP_ASSIGN)    TRYCATCH(REF_assign(REF_HEAD(ref),deq(VMRES_WIP,POP)),op,bc_exc,"assigning to ref"); continue;
                    OPCODE(VMOP_REMOVE)    TRYCATCH(REF_remove(REF_HEAD(ref)),op,bc_exc,"removing from ref");                   continue;
                    OPCODE(VMOP_APPEND)    continue;
                    OPCODE(VMOP_COMPARE)   continue;

                    OPCODE(VMOP_RDLOCK)    pthread_rwlock_rdlock(&vm_rwlock); continue;
                    OPCODE(VMOP_WRLOCK)    pthread_rwlock_wrlock(&vm_rwlock); continue;
                    OPCODE(VMOP_UNLOCK)    pthread_rwlock_unlock(&vm_rwlock); continue;

                    OPCODE(VMOP_BYTECODE)  TRYCATCH(!env_enq(res_ltv,VMRES_CODE,deq(VMRES_WIP,POP)),op,bc_exc,"pushing bytecode"); continue;

                    OPCODE(VMOP_EDICT)     enq(VMRES_WIP,compile_ltv(compilers[FORMAT_edict],  deq(VMRES_WIP,POP))); continue;
                    OPCODE(VMOP_XML)       enq(VMRES_WIP,compile_ltv(compilers[FORMAT_xml],    deq(VMRES_WIP,POP))); continue;
                    OPCODE(VMOP_JSON)      enq(VMRES_WIP,compile_ltv(compilers[FORMAT_json],   deq(VMRES_WIP,POP))); continue;
                    OPCODE(VMOP_YAML)      enq(VMRES_WIP,compile_ltv(compilers[FORMAT_yaml],   deq(VMRES_WIP,POP))); continue;
                    OPCODE(VMOP_SWAGGER)   enq(VMRES_WIP,compile_ltv(compilers[FORMAT_swagger],deq(VMRES_WIP,POP))); continue;
                    OPCODE(VMOP_LISP)      enq(VMRES_WIP,compile_ltv(compilers[FORMAT_lisp],   deq(VMRES_WIP,POP))); continue;
                    OPCODE(VMOP_MASSOC)    enq(VMRES_WIP,compile_ltv(compilers[FORMAT_massoc], deq(VMRES_WIP,POP))); continue;

                    OPCODE(VMOP_YIELD)     goto yield;
                    OPCODE(VMOP_MMAP_KEEP) TRYCATCH(map_make(KEEP),op,bc_exc,"evaluating map_make"); continue;
                    OPCODE(VMOP_MMAP_POP)  TRYCATCH(map_make(POP),op,bc_exc,"evaluating map_make"); continue;
                    OPCODE(VMOP_MAP_KEEP)  TRYCATCH(map(KEEP),op,bc_exc,"evaluating map-keep"); goto yield;
                    OPCODE(VMOP_MAP_POP)   TRYCATCH(map(POP),op,bc_exc,"evaluating map-pop"); goto yield;

                    /*
                      case VMOP_REF_ITER_KEEP: REF_iterate(LTV_list(ref),KEEP); continue;
                      case VMOP_REF_ITER_POP: REF_iterate(LTV_list(ref),POP); continue;
                      case VMOP_GRAPH_STACK: graph_ltvs_to_file("/tmp/jj.dot",LTV_list(deq(VMRES_STACK,KEEP)),0,"tos"); continue;
                      case VMOP_PRINT_REFS:  REF_printall(stdout,LTV_list(ref),"Ref: "); continue;
                      case VMOP_GRAPH_REFS:  REF_dot(stdout,LTV_list(ref),"Ref: "); continue;
                    */

                    default: STRY(((*env_state)=VM_ERROR),"evaluating invalid bytecode 0x%x",op); continue;
                }
            }

        bc_exc:
            status=0;
            throw(LTV_NULL);
        }

    yield:
        if ((*ip)>=len)
            release_code();
    }

    (*env_state)&=~VM_BYPASS;

 done:
    return status;
}


//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////
LTV *vm_env_create(LTV *root,LTV *code)
{
    int status=0;

    LTV *env_cvar=NULL;
    STRY(!(env_cvar=cif_create_cvar(cif_type_info("((LTV)*)*"),NEW(LTV *[VMRES_COUNT]),NULL)),"creating env cvar");

    LTV **res_ltv=env_cvar->data; // sneaky;
    for (int i=0;i<VMRES_CLL_COUNT;i++)
        STRY(!(res_ltv[i]=LTV_NULL_LIST),"creating %s",res_name[i]);
    for (int i=VMRES_CLL_COUNT;i<VMRES_COUNT;i++)
        STRY(!(res_ltv[i]=cif_create_cvar(cif_type_info("int"),NULL,NULL)),"creating %s",res_name[i]);

    STRY(!env_enq(res_ltv,VMRES_STACK,LTV_NULL_LIST),"initializing stack");
    STRY(!env_enq(res_ltv,VMRES_CODE,code),"pushing code");

    // construct dictionary
    LTV *env_ltv=LTV_init(NEW(LTV),"ENV",-1,LT_RO);
    LT_put(env_ltv,"ENV",HEAD,env_cvar);
    for (int i=0;i<VMRES_COUNT;i++)
        LT_put(env_ltv,res_name[i],HEAD,res_ltv[i]);

    STRY(!env_enq(res_ltv,VMRES_DICT,env_ltv),"pushing environment into dict");
    STRY(!env_enq(res_ltv,VMRES_DICT,root),"pushing reflection into dict");
 done:
    if (status)
        LTV_release(env_cvar);
    return env_cvar;
}

int vm_env_enq(LTV *envs,LTV *env_ltv)
{
    int status=0;
    STRY(!LTV_enq(LTV_list(envs),env_ltv,TAIL),"enqueing process env");
    sem_post(&vm_process_escapement);
 done:
    return status;
}

LTV *vm_env_deq(LTV *envs)
{
    sem_wait(&vm_process_escapement);
    return LTV_deq(LTV_list(envs),HEAD);
}

void *vm_thunk(void *udata)
{
    int status=0;
    LTV *envs=(LTV *) udata;
    LTV *env_ltv=NULL;
    do {
        STRY(!(env_ltv=vm_env_deq(envs)),"popping env");
        STRY(vm_eval(env_ltv),"evaluating env");
        STRY(!vm_env_enq(envs,env_ltv),"pushing env");
    } while (1);
 done:
    return status?NON_NULL:NULL;
}

int vm_run()
{
    int status=0;
    LTV *envs=LTV_NULL_LIST; // list of environments

    char *bootstrap_code="[hello, world!] #dump";
    LTV *code=compile(compilers[FORMAT_edict],bootstrap_code,-1); // code stack

    STRY(vm_env_enq(envs,vm_env_create(cif_module,code)),"pushing env");
    vm_thunk(envs);
 done:
    LTV_release(envs);
    return status;
}
