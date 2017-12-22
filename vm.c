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

char *res_name[] = { "dict","refs","code","ip","wip","stack" };

LTV *vm_enq(VM_ENV *env,int res,LTV *tos) { return LTV_put(&env->res[res],tos,HEAD,NULL); }
LTV *vm_deq(VM_ENV *env,int res,int pop) { return LTV_get(&env->res[res],pop,HEAD,NULL,NULL); }

//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////
int vm_env_init(VM_ENV *env,LTV *tos)
{
    int status=0;
    ZERO(*env);
    LTV_init(&env->lnk,"env0",-1,LT_NONE);
    for (int res=0;res<VMRES_COUNT;res++)
        CLL_init(&env->res[res]);
    STRY(!vm_enq(env,VMRES_DICT,tos),"initializing dict");
    STRY(!vm_enq(env,VMRES_STACK,LTV_NULL_LIST),"initializing stack");
 done:
    return status;
}

int vm_env_release(VM_ENV *env)
{
    for (int res=0;res<VMRES_COUNT;res++) {
        CLL_release(&env->res[res],LTVR_release);
    }
}

int vm_env_enq(LTV *envs,VM_ENV *env,LTV *lambda)
{
    int status=0;
    if (lambda) {
        STRY(!vm_enq(env,VMRES_CODE,lambda),"initializing code");
        STRY(!vm_enq(env,VMRES_IP,LTV_ZERO),"pushing IP");
    }
    STRY(!LTV_enq(LTV_list(envs),&env->lnk,TAIL),"enqueing process env");
    sem_post(&vm_process_escapement);
 done:
    return status;
}

VM_ENV *vm_env_deq(LTV *envs)
{
    sem_wait(&vm_process_escapement);
    return (VM_ENV *) LTV_deq(LTV_list(envs),HEAD);
}

void *vm_thunk(void *udata)
{
    LTV *envs=(LTV *) udata;
    VM_ENV *env=NULL;
    while ((env=vm_env_deq(envs)) && !vm_eval(env) && !vm_env_enq(envs,env,NULL));
 done:
    return env && env->state?NON_NULL:NULL;
}


//////////////////////////////////////////////////
extern int square(int a);
int square(int a) { return a*a; }
//////////////////////////////////////////////////


//////////////////////////////////////////////////
// Bytecode Interpreter
//////////////////////////////////////////////////
int vm_eval(VM_ENV *env)
{
    int status=0;

    LTV *enq(int res,LTV *tos) { return vm_enq(env,res,tos); }
    LTV *deq(int res,int pop) { return vm_deq(env,res,pop); }

    LTV *stack_enq(LTV *ltv)
    {
        return LTV_enq(LTV_list(deq(VMRES_STACK,KEEP)),ltv,HEAD);
    }

    LTV *stack_deq(int pop)
    {
        void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
        return CLL_map(&env->res[VMRES_STACK],FWD,op);
    }

    int context_push()
    {
        int status=0;
        STRY(!enq(VMRES_DICT,stack_deq(POP)),"pushing dict level");
        STRY(!enq(VMRES_STACK,LTV_NULL_LIST),"pushing stack level");
    done:
        return status;
    }

    int context_pop()
    {
        int status=0;
        LTV *oldstack=deq(VMRES_STACK,POP);
        LTV *newstack=deq(VMRES_STACK,KEEP);
        CLL_MERGE(LTV_list(newstack),LTV_list(oldstack),HEAD);
        LTV_release(oldstack);
        STRY(!stack_enq(deq(VMRES_DICT,POP)),"returning dict context to stack");
    done:
        return status;
    }

    int lambda_push(LTV *ltv)
    {
        int status=0;
        STRY(!enq(VMRES_CODE,ltv),"pushing code");
        STRY(!enq(VMRES_IP,LTV_ZERO),"pushing IP");
    done:
        return status;
    }

    int lambda_pop()
    {
        int status=0;
        LTV_release(deq(VMRES_CODE,POP));
        LTV_release(deq(VMRES_IP,POP));
    done:
        return status;
    }

    int dump()
    {
        int status=0;
        char filename[256];
        for (int i=0;i<VMRES_COUNT;i++)
        {
            printf("\n%s:\n",res_name[i]);
            print_ltvs(stdout,CODE_RED,&env->res[i],CODE_RESET "\n",0);
            sprintf(filename,"/tmp/vm_%s.dot",res_name[i]);
            graph_ltvs_to_file(filename,&env->res[i],5,res_name[i]);
        }
        return status;
    }

    int ref_hres(CLL *cll,LTV *ref)
    {
        int status=0;
        LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
        void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
        STRY(!CLL_map(cll,FWD,op),"resolving ref");
    done:
        return status;
    }

    int dumpstack()
    {
        int status=0;
        print_ltvs(stdout,CODE_RED,&env->res[VMRES_STACK],CODE_RESET "\n",0);
        graph_ltvs_to_file("/tmp/vm_tos.dot",&env->res[VMRES_STACK],0,"TOS");
    done:
        return status;
    }

    int ffi(LTV *lambda) // adapted from edict.c's ffi_eval(...)
    {
        int status=0;
        CLL args; CLL_init(&args); // list of ffi arguments
        LTV *rval=NULL;
        STRY(!(rval=ref_rval_create(lambda)),"creating ffi rval ltv");
        int marshal(char *name,LTV *type) {
            int status=0;
            LTV *arg=NULL, *coerced=NULL;
            STRY(!(arg=stack_deq(POP)),"popping ffi arg from stack"); // FIXME: attempt to resolve by name first
            STRY(!(coerced=ref_coerce(arg,type)),"coercing ffi arg");
            LTV_enq(&args,coerced,HEAD); // enq coerced arg onto args CLL
            LT_put(rval,name,HEAD,coerced); // coerced args are installed as childen of rval
            LTV_release(arg);
        done:
            return status;
        }
        STRY(ref_args_marshal(lambda,marshal),"marshalling ffi args"); // pre-
        STRY(ref_ffi_call(lambda,rval,&args),"calling ffi");
        STRY(!stack_enq(rval),"enqueing rval onto stack");
        CLL_release(&args,LTVR_release);
    done:
        return status;
    }

    int cvar()
    {
        int status=0;
        LTV *type,*cvar;
        STRY(!(type=stack_deq(POP)),"popping type");
        STRY(!(cvar=ref_create_cvar(type,NULL,NULL)),"creating cvar");
        STRY(!stack_enq(cvar),"pushing cvar");
    done:
        if (type)
            LTV_release(type);
        return status;
    }

    int builtin()
    {
        int status=0;
        LTV *tmp=NULL;
        printf("builtin:\n");
        LTV *ref=deq(VMRES_REFS,POP);
        if (!strncmp("dump",ref->data,ref->len))
            dump(env);
        else if (!strncmp("ref",ref->data,ref->len))
            tmp=stack_enq(ref_mod);
        else if (!strncmp("mod",ref->data,ref->len))
            dump_module_simple("/tmp/module.dot",ref_mod);
        else if (!strncmp("new",ref->data,ref->len))
            STRY(cvar(env),"allocating cvar");
        LTV_release(ref);
    done:
        return status;
    }


    LTV *code_ltv=NULL,*ip_ltv=NULL;
    STRY(!env,"validating environment");
    if (!(code_ltv=deq(VMRES_CODE,KEEP)) || !(ip_ltv=deq(VMRES_IP,KEEP))) {
        status=!0;
        goto done;
    }

    if (code_ltv->flags&LT_CVAR) {
        STRY(ffi(code_ltv),"executing ffi");
    } else {
        char *data=(char *) code_ltv->data;
        int len=code_ltv->len;
        int *ip=(int *) &ip_ltv->data;

        LTV *decode_extended() {
            unsigned length,flags;
            VM_BC_LTV *extended=NULL;
            extended=(VM_BC_LTV *) (data+(*ip));
            length=ntohl(extended->length);
            flags=ntohl(extended->flags);
            (*ip)+=(sizeof(length)+sizeof(flags)+length);
            return LTV_init(NEW(LTV),extended->data,length,flags);
        }

        unsigned char op=0,res=0;;

#define OPCODE(vmop) case (unsigned char) vmop: printf(CODE_RED "0x%x\n" CODE_RESET,(unsigned char) vmop);

        while ((*ip)<len && data[*ip]) {
            op=data[(*ip)++];
            switch(op) {
                OPCODE(VMOP_RES_DICT)  res=VMRES_DICT;  break;
                OPCODE(VMOP_RES_REFS)  res=VMRES_REFS;  break;
                OPCODE(VMOP_RES_CODE)  res=VMRES_CODE;  break;
                OPCODE(VMOP_RES_IP)    res=VMRES_IP;    break;
                OPCODE(VMOP_RES_WIP)   res=VMRES_WIP;   break;
                OPCODE(VMOP_RES_STACK) res=VMRES_STACK; break;

                OPCODE(VMOP_SPUSH)     STRY(!stack_enq(deq(VMRES_WIP,POP)),"SPUSH'ing"); break;
                OPCODE(VMOP_SPOP)      STRY(!enq(VMRES_WIP,stack_deq(POP)),"SPOP'ing"); break;
                OPCODE(VMOP_SPEEK)     STRY(!enq(VMRES_WIP,stack_deq(KEEP)),"SPEEK'ing"); break;

                OPCODE(VMOP_PUSH)      enq(res,deq(VMRES_WIP,POP)); break;
                OPCODE(VMOP_POP)       enq(VMRES_WIP,deq(res,POP)); break;
                OPCODE(VMOP_PEEK)      enq(VMRES_WIP,deq(res,KEEP)); break;
                OPCODE(VMOP_DUP)       enq(res,deq(res,KEEP)); break;
                OPCODE(VMOP_DROP)      LTV_release(deq(res,POP)); break;

                OPCODE(VMOP_LIT)       enq(VMRES_WIP,decode_extended()); break;
                OPCODE(VMOP_BUILTIN)   builtin(env); break;
                OPCODE(VMOP_TOS)       dumpstack(env); break;
                OPCODE(VMOP_YIELD)     goto done; // break out of loop, requeue env;

                OPCODE(VMOP_REF_MAKE)  STRY(!enq(VMRES_REFS,REF_create(deq(VMRES_WIP,POP))),"making a ref"); break;
                OPCODE(VMOP_REF_INS)   STRY(REF_resolve(deq(VMRES_DICT,KEEP),deq(VMRES_REFS,KEEP),TRUE),"inserting ref"); break;
                OPCODE(VMOP_REF_RES)   STRY(REF_resolve(deq(VMRES_DICT,KEEP),deq(VMRES_REFS,KEEP),FALSE),"resolving ref"); break;
                OPCODE(VMOP_REF_HRES)  STRY(ref_hres(&env->res[VMRES_DICT],deq(VMRES_REFS,KEEP)),"hierarchically resolving ref"); break;
                OPCODE(VMOP_REF_ITER)  STRY(REF_iterate(deq(VMRES_REFS,KEEP),KEEP),"iterating ref"); break;
                OPCODE(VMOP_DEREF)     STRY(!enq(VMRES_WIP,REF_ltv(REF_HEAD(deq(VMRES_REFS,KEEP)))),"dereferencing"); break;
                OPCODE(VMOP_ASSIGN)    STRY(REF_assign(REF_HEAD(deq(VMRES_REFS,KEEP)),deq(VMRES_WIP,POP)),"assigning to ref"); break;
                OPCODE(VMOP_REMOVE)    STRY(REF_remove(REF_HEAD(deq(VMRES_REFS,KEEP))),"removing from ref"); break;
                OPCODE(VMOP_THROW)     break;
                OPCODE(VMOP_CATCH)     break;
                OPCODE(VMOP_MAP)       break;
                OPCODE(VMOP_APPEND)    break;
                OPCODE(VMOP_COMPARE)   break;

                OPCODE(VMOP_ENFRAME)   STRY(context_push(),"pushing context"); break;
                OPCODE(VMOP_DEFRAME)   STRY(context_pop(),"popping context"); break;

                OPCODE(VMOP_RDLOCK)    pthread_rwlock_rdlock(&vm_rwlock); break;
                OPCODE(VMOP_WRLOCK)    pthread_rwlock_wrlock(&vm_rwlock); break;
                OPCODE(VMOP_UNLOCK)    pthread_rwlock_unlock(&vm_rwlock); break;

                OPCODE(VMOP_EDICT)     lambda_push(compile_ltv(compilers[FORMAT_edict],  deq(VMRES_WIP,POP))); goto done;
                OPCODE(VMOP_XML)       lambda_push(compile_ltv(compilers[FORMAT_xml],    deq(VMRES_WIP,POP))); goto done;
                OPCODE(VMOP_JSON)      lambda_push(compile_ltv(compilers[FORMAT_json],   deq(VMRES_WIP,POP))); goto done;
                OPCODE(VMOP_YAML)      lambda_push(compile_ltv(compilers[FORMAT_yaml],   deq(VMRES_WIP,POP))); goto done;
                OPCODE(VMOP_SWAGGER)   lambda_push(compile_ltv(compilers[FORMAT_swagger],deq(VMRES_WIP,POP))); goto done;
                OPCODE(VMOP_LISP)      lambda_push(compile_ltv(compilers[FORMAT_lisp],   deq(VMRES_WIP,POP))); goto done;
                OPCODE(VMOP_MASSOC)    lambda_push(compile_ltv(compilers[FORMAT_massoc], deq(VMRES_WIP,POP))); goto done;
                /*
                  case VMOP_REF_ITER_KEEP: REF_iterate(LTV_list(deq(VMRES_REFS,KEEP)),KEEP); break;
                  case VMOP_REF_ITER_POP: REF_iterate(LTV_list(deq(VMRES_REFS,KEEP)),POP); break;
                  case VMOP_GRAPH_STACK: graph_ltvs_to_file("/tmp/jj.dot",LTV_list(deq(VMRES_STACK,KEEP)),0,"tos"); break;
                  case VMOP_PRINT_REFS:  REF_printall(stdout,LTV_list(deq(VMRES_REFS,KEEP)),"Ref: "); break;
                  case VMOP_GRAPH_REFS:  REF_dot(stdout,LTV_list(deq(VMRES_REFS,KEEP)),"Ref: "); break;
                */

                default: STRY((env->state=ENV_BROKEN),"evaluating invalid bytecode 0x%x",op); break;;
            }
        }
    }

    lambda_pop(env);

 done:
    return status;
}

int vm_thread(LTV *env,LTV *code)
{
}

int vm_init(int argc,char *argv[])
{
    int status=0;
    LTV *dict=LTV_init(NEW(LTV),"DICT",-1,LT_RO|LT_NONE);
    LTV *envs=LTV_init(NEW(LTV),"ENVS",-1,LT_RO|LT_LIST);
    LT_put(dict,"ENVS",HEAD,envs);
    LT_put(dict,"self",HEAD,ref_mod);

    VM_ENV *env=NEW(VM_ENV);
    STRY(vm_env_init(env,dict),"initializing env"); // initial env is dict-root+bootstrap-code

////////////////////////////////////////////////////////////////////////////////////////////////////

    int format=FORMAT_edict;
    FILE *file=stdin;
    char *data;
    int len;
    LTV *ltv=NULL;

    switch (argc)
    {
        case 3:
            file=fopen(argv[2],"r");
            // FALL THRU!
        case 2:
            for (int format=0;format<FORMAT_MAX;format++)
                if (!strcmp(formats[format],argv[1]))
                    break;
            break;
        case 1:
            printf("Specify format (asm | edict | xml | json | yaml | mmassoc)\n");
            break;
    }

    do {
        TRYCATCH((data=balanced_readline(file,&len))==NULL,0,close_file,"reading balanced line from file");
        TRYCATCH(!(ltv=compile(compilers[format],data,len)),TRY_ERR,free_data,"compiling balanced line");
        print_ltv(stdout,"bytecodes: ",ltv,"\n",0);
        TRYCATCH(vm_env_enq(envs,env,ltv),TRY_ERR,free_data,"pushing env/lambda");
        vm_thunk(envs);
    free_data:
        DELETE(data);
    } while(1);

 close_file:
    if (file!=stdin)
        fclose(file);

 release_env:
    printf("releasing env\n");
    vm_env_release(env);

////////////////////////////////////////////////////////////////////////////////////////////////////

 done:
    return status;
}
