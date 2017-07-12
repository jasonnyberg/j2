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


sem_t vm_process_access,vm_process_escapement;
pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

__attribute__((constructor))
static void init(void)
{
    sem_init(&vm_process_access,1,0); // allows first wait through
    sem_init(&vm_process_escapement,0,0); // blocks on first wait

    Dl_info dl_info;
    dladdr((void *)init, &dl_info);
    fprintf(stderr, CODE_RED "module name is: %s" CODE_RESET "\n", dl_info.dli_fname);
}

#define RDLOCK pthread_rwlock_rdlock(&vm_rwlock)
#define WRLOCK pthread_rwlock_wrlock(&vm_rwlock)
#define UNLOCK pthread_rwlock_unlock(&vm_rwlock)

//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////
int vm_env_enq(LTV *root,VM_ENV *env)
{
    int status=0;
    STRY(!LTV_enq(&root->sub.ltvs,&env->ltv,TAIL),"enqueing process env");
    sem_post(&vm_process_escapement);
 done:
    return status;
}

VM_ENV *vm_env_deq(LTV *root)
{
    sem_wait(&vm_process_escapement);
    return (VM_ENV *) LTV_deq(&root->sub.ltvs,HEAD);
}

void *vm_thunk(void *udata)
{
    int status=0;
    LTV *root=(LTV *) udata;
    VM_ENV *env=NULL;
    while ((env=vm_env_deq(root)) && (status=vm_eval(env)) && !vm_env_enq(root,env))
        printf("vm_thumk env status=%d\n",status);
    printf("releasing env\n");
    LTV_release(&env->ltv);
 done:
    return status?NON_NULL:NULL;
}

//////////////////////////////////////////////////
// Environment
//////////////////////////////////////////////////
LTV *vm_res_put(VM_ENV *env,int res,LTV *tos) { return LTV_enq(&env->ros[res],env->tos[res],HEAD),env->tos[res]=tos; }
LTV *vm_res_get(VM_ENV *env,int res,int pop) { LTV *tos=env->tos[res]; if (pop) env->tos[res]=LTV_deq(&env->ros[res],HEAD); return tos; }

LTV *vm_res_qput(VM_ENV *env,int res,LTV *ltv) { return LTV_enq(&env->tos[res]->sub.ltvs,ltv,HEAD); }
LTV *vm_res_qget(VM_ENV *env,int res,int pop) { return LTV_get(&env->tos[res]->sub.ltvs,pop,HEAD,NULL,NULL); }

LTV *vm_tos_put(VM_ENV *env,LTV *ltv) { return vm_res_qput(env,VMRES_STACK,ltv); }
LTV *vm_tos_get(VM_ENV *env,int pop)
{
    void *op(CLL *lnk) { return LTV_get(&((LTV *) lnk)->sub.ltvs,pop,HEAD,NULL,NULL); }
    LTV *tos=vm_res_qget(env,VMRES_STACK,pop);
    return tos?tos:CLL_map(&env->ros[VMRES_STACK],FWD,op);
}

LTV *vm_context_push(VM_ENV *env,LTV *ltv)
{
    vm_res_put(env,VMRES_DICT,ltv);
    vm_res_put(env,VMRES_STACK,LTV_NULL_LIST);
}

LTV *vm_context_pop(VM_ENV *env)
{
    return NULL;
}

int vm_env_init(VM_ENV *env,LTV *seed,LTV *code)
{
    int status=0;
    ZERO(*env);
    LTV_init(&env->ltv,"env0",-1,LT_NONE);
    for (int res=0;res<VMRES_COUNT;res++)
        CLL_init(&env->ros[res]);

    STRY(!vm_res_put(env, VMRES_STACK, LTV_NULL_LIST),"pushing stack root");
    STRY(!vm_res_put(env, VMRES_CODE,  code),"pushing code");
    STRY(!vm_res_put(env, VMRES_IP,    LTV_ZERO),"pushing IP");
    STRY(!vm_res_put(env, VMRES_DICT,  LTV_NULL_LIST),"pushing dict root");

    STRY(!vm_res_qput(env,VMRES_STACK, seed),"enqueing env's stack seed");
 done:
    return status;
}

//////////////////////////////////////////////////
// Bytecode Interpreter
//////////////////////////////////////////////////
int vm_eval(VM_ENV *env)
{
    int status=0;
    STRY(!env || !env->tos[VMRES_CODE] || !env->tos[VMRES_IP],"validating environment");

    char *data=(char *) env->tos[VMRES_CODE]->data;
    int len=env->tos[VMRES_CODE]->len;
    int *ip=(int *) &env->tos[VMRES_IP]->data;

    unsigned length,flags;
    LTV *ltv=NULL;
    VM_BC_LTV *extended=NULL;

    for (;!status && (*ip)<len && data[*ip];(*ip)++) {
        switch(data[*ip]) {
            case VMOP_LTV: // length!; data is a lit
                extended=(VM_BC_LTV *) (data+(*ip)+1);
                length=ntohl(extended->length);
                flags=ntohl(extended->flags);
                STRY(!(ltv=LTV_init(NEW(LTV),extended->data,length,flags)),"creating ltv");
                (*ip)+=(sizeof(length)+sizeof(flags)+length);
                STRY(!LTV_enq(&env->tos[VMRES_STACK]->sub.ltvs,ltv,HEAD),"pushing ltv to stack");
                break;

            case VMOP_YIELD:
                break;

            case VMOP_SCOPE_OPEN: WRLOCK;

                UNLOCK; break;
            case VMOP_SCOPE_CLOSE: WRLOCK;
                UNLOCK; break;

            case VMOP_FUNCTION_OPEN: WRLOCK;
                UNLOCK; break;
            case VMOP_FUNCTION_CLOSE: WRLOCK;
                UNLOCK; break;

            case VMOP_REF_CREATE: RDLOCK; // push current ref tos, peek stack, create/resolve new ref, pop/release stack on success, releas
                TRYCATCH(!(ltv=vm_tos_get(env,POP)),status,ref_create_done,"peeking TOS");
                TRYCATCH(!LTV_enq(&env->ros[VMRES_REF],env->tos[VMRES_REF],HEAD),status,ref_create_done,"pushing ref");
                TRYCATCH(!(env->tos[VMRES_REF]=LTV_NULL_LIST),status,ref_create_done,"creating new ref");
                REF_create(ltv->data,ltv->len,&env->tos[VMRES_REF]->sub.ltvs);
            ref_create_done:
                UNLOCK; break;
            case VMOP_REF_INSERT: WRLOCK;
                TRYCATCH(REF_resolve(env->tos[VMRES_DICT],&env->tos[VMRES_REF]->sub.ltvs,true),status,ref_insert_done,"resolving ref");
            ref_insert_done:
                UNLOCK; break;
            case VMOP_REF_RESOLVE: RDLOCK;
                TRYCATCH(REF_resolve(env->tos[VMRES_DICT],&env->tos[VMRES_REF]->sub.ltvs,false),status,ref_resolve_done,"resolving ref");
            ref_resolve_done:
                UNLOCK; break;
            case VMOP_REF_RRESOLVE: RDLOCK;
                UNLOCK; break;
            case VMOP_REF_ITER_KEEP: RDLOCK;
                TRYCATCH(REF_iterate(&env->tos[VMRES_REF]->sub.ltvs,false),status,ref_iter_keep_done,"iterating ref");
            ref_iter_keep_done:
                UNLOCK; break;
            case VMOP_REF_ITER_POP: WRLOCK;
                TRYCATCH(REF_iterate(&env->tos[VMRES_REF]->sub.ltvs,true),status,ref_iter_pop_done,"iterating ref");
            ref_iter_pop_done:
                UNLOCK; break;
            case VMOP_REF_ASSIGN:  WRLOCK;
                TRYCATCH(REF_assign(REF_HEAD(&env->tos[VMRES_REF]->sub.ltvs),ltv),status,ref_assign_done,"assigning ref");
            ref_assign_done:
                UNLOCK; break;
            case VMOP_REF_REMOVE: WRLOCK;
                TRYCATCH(REF_remove(REF_HEAD(&env->tos[VMRES_REF]->sub.ltvs)),status,ref_remove_done,"removing ref");
            ref_remove_done:
                UNLOCK; break;
            case VMOP_REF_DELETE: // delete refs, delete ref, pop new ref tos from ros WRLOCK;
                TRYCATCH(REF_delete(&env->tos[VMRES_REF]->sub.ltvs),status,ref_delete_done,"deleting ref");
                LTV_release(env->tos[VMRES_REF]);
                TRYCATCH(!(env->tos[VMRES_REF]=(LTV *) CLL_get(&env->ros[VMRES_REF],POP,HEAD)),status,ref_delete_done,"popping ref");
            ref_delete_done:
                UNLOCK; break;

            case VMOP_PRINT_STACK: RDLOCK;
                print_ltvs(stdout,CODE_BLUE,&env->tos[VMRES_STACK]->sub.ltvs,CODE_RESET "\n",0);
                UNLOCK; break;
            case VMOP_GRAPH_STACK: RDLOCK;
                graph_ltvs_to_file("/tmp/jj.dot",&env->tos[VMRES_STACK]->sub.ltvs,0,"tos");
                UNLOCK; break;
            default:
                break;
        }
    }

 done:
    return status;
}

//////////////////////////////////////////////////
// Assembler
//////////////////////////////////////////////////
LTV *vm_asm(VM_CMD cmd[])
{
    char *buf=NULL;
    size_t len=0;
    FILE *stream=open_memstream(&buf,&len);
    unsigned unsigned_val;
    for (;cmd->op;cmd++) {
        fwrite(&cmd->op,1,1,stream);
        switch (cmd->len) {
            case 0: break;
            case -1:
                cmd->len=strlen(cmd->data); // rewrite len and...
                // ...fall thru!
            default:
                unsigned_val=htonl(cmd->len);
                fwrite(&unsigned_val,sizeof(unsigned_val),1,stream);
                unsigned_val=htonl(cmd->flags);
                fwrite(&unsigned_val,sizeof(unsigned_val),1,stream);
                fwrite(cmd->data,1,cmd->len,stream);
                break;
        }
    }
    fputc(0,stream);
    fclose(stream);
    return LTV_init(NEW(LTV),buf,len,LT_OWN|LT_BIN);
}

VM_CMD test[] = {
    { VMOP_LTV,-1,LT_DUP,"test item 1" },
    { VMOP_LTV,-1,LT_DUP,"test item 2" },
    { VMOP_LTV,-1,LT_DUP,"test item 3" },
    { VMOP_LTV,-1,LT_DUP,"test item 4" },
    { VMOP_PRINT_STACK },
    { VMOP_NULL }
};

int vm_thread(LTV *env,LTV *code)
{
}

int vm_init(int argc,char *argv[])
{
    int status=0;
    LTV *root=LTV_NULL_LIST;

    VM_ENV *env=NEW(VM_ENV);
    vm_env_init(env,root,vm_asm(test)); // initial env is root LTV
    vm_env_enq(root,env);
    vm_thunk(root);

 done:
    return status;
}
