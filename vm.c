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
}

//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////
int vm_env_enq(LTV *root,VM_ENV *env)
{
    int status=0;
    STRY(!LTV_enq(LTV_list(root),&env->ltv,TAIL),"enqueing process env");
    sem_post(&vm_process_escapement);
 done:
    return status;
}

VM_ENV *vm_env_deq(LTV *root)
{
    sem_wait(&vm_process_escapement);
    return (VM_ENV *) LTV_deq(LTV_list(root),HEAD);
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
LTV *vm_res_enq(VM_ENV *env,int res,LTV *tos) { return LTV_enq(&env->ros[res],env->tos[res],HEAD),env->tos[res]=tos; }
LTV *vm_res_deq(VM_ENV *env,int res,int pop) { LTV *tos=env->tos[res]; if (pop) env->tos[res]=LTV_deq(&env->ros[res],HEAD); return tos; }

LTV *vm_res_qenq(VM_ENV *env,int res,LTV *ltv) { return LTV_enq(LTV_list(env->tos[res]),ltv,HEAD); }
LTV *vm_res_qdeq(VM_ENV *env,int res,int pop) { return LTV_get(LTV_list(env->tos[res]),pop,HEAD,NULL,NULL); }

LTV *vm_tos_enq(VM_ENV *env,LTV *ltv) { return vm_res_qenq(env,VMRES_STACK,ltv); }
LTV *vm_tos_deq(VM_ENV *env,int pop)
{
    void *op(CLL *lnk) { return LTV_get(LTV_list(((LTVR *) lnk)->ltv),pop,HEAD,NULL,NULL); }
    LTV *tos=vm_res_qdeq(env,VMRES_STACK,pop);
    return tos?tos:CLL_map(&env->ros[VMRES_STACK],FWD,op);
}

int vm_ref_resolve(VM_ENV *env,int insert)
{
    LTV *ref_ltv=vm_res_deq(env,VMRES_REF,KEEP);
    LTV *resolve_ltv(LTV *root) { return REF_resolve(root,LTV_list(ref_ltv),insert)?NULL:REF_ltv(REF_HEAD(LTV_list(ref_ltv))); }
    void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
    LTV *result=resolve_ltv(vm_res_deq(env,VMRES_DICT,KEEP));
    if (!result)
        result=CLL_map(&env->ros[VMRES_STACK],FWD,op);
    if (result)
        vm_tos_enq(env,result);
    else { // autolit
        REF_delete(LTV_list(ref_ltv));
        vm_tos_enq(env,vm_res_deq(env,VMRES_REF,POP));
    }
    return 0;
}

LTV *vm_context_push(VM_ENV *env,LTV *ltv)
{
    vm_res_enq(env,VMRES_DICT,ltv);
    vm_res_enq(env,VMRES_STACK,LTV_NULL_LIST);
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

    STRY(!vm_res_enq(env, VMRES_STACK, LTV_NULL_LIST),"pushing stack root");
    STRY(!vm_res_enq(env, VMRES_CODE,  code),"pushing code");
    STRY(!vm_res_enq(env, VMRES_IP,    LTV_ZERO),"pushing IP");
    STRY(!vm_res_enq(env, VMRES_DICT,  LTV_NULL_LIST),"pushing dict root");

    STRY(!vm_res_qenq(env,VMRES_STACK, seed),"enqueing env's stack seed");
 done:
    return status;
}

//////////////////////////////////////////////////
// Bytecode Interpreter
//////////////////////////////////////////////////
int vm_eval(VM_ENV *env)
{
    int state=0;

    char *data=(char *) env->tos[VMRES_CODE]->data;
    int len=env->tos[VMRES_CODE]->len;
    int *ip=(int *) &env->tos[VMRES_IP]->data;

    unsigned length,flags;
    LTV *ltv=NULL;
    VM_BC_LTV *extended=NULL;

    while ((*ip)<len && data[*ip]) {
        switch(data[(*ip)++]) {
            case VMOP_RDLOCK: pthread_rwlock_rdlock(&vm_rwlock); break;
            case VMOP_WRLOCK: pthread_rwlock_wrlock(&vm_rwlock); break;
            case VMOP_UNLOCK: pthread_rwlock_unlock(&vm_rwlock); break;

            case VMOP_LTV: // length! data is a lit
                extended=(VM_BC_LTV *) (data+(*ip));
                length=ntohl(extended->length);
                flags=ntohl(extended->flags);
                (*ip)+=(sizeof(length)+sizeof(flags)+length);
                vm_tos_enq(env,LTV_init(NEW(LTV),extended->data,length,flags));
                break;
            case VMOP_DUP:
                vm_tos_enq(env,LTV_dup(vm_tos_deq(env,KEEP)));
                break;

            case VMOP_REF_CREATE:
                ltv=vm_res_enq(env,VMRES_REF,vm_tos_deq(env,KEEP));
                REF_create(ltv->data,ltv->len,LTV_list(ltv));
                break;
            case VMOP_REF_INSERT:
                vm_ref_resolve(env,true);
                break;
            case VMOP_REF_RESOLVE:
                vm_ref_resolve(env,false);
                break;
            case VMOP_REF_ITER_KEEP:
                REF_iterate(LTV_list(vm_res_deq(env,VMRES_REF,KEEP)),KEEP);
                break;
            case VMOP_REF_ITER_POP:
                REF_iterate(LTV_list(vm_res_deq(env,VMRES_REF,KEEP)),POP);
                break;
            case VMOP_REF_ASSIGN:
                REF_assign(REF_HEAD(LTV_list(vm_res_deq(env,VMRES_REF,KEEP))),vm_tos_deq(env,POP));
                break;
            case VMOP_REF_REMOVE:
                REF_remove(REF_HEAD(LTV_list(vm_res_deq(env,VMRES_REF,KEEP))));
                break;
            case VMOP_REF_DELETE: // delete refs, delete ref, pop new ref tos from ros
                REF_delete(LTV_list(env->tos[VMRES_REF]));
                LTV_release(env->tos[VMRES_REF]);
                env->tos[VMRES_REF]=(LTV *) CLL_get(&env->ros[VMRES_REF],POP,HEAD);
                break;

            case VMOP_EVAL: break;

            case VMOP_SCOPE_OPEN: break;
            case VMOP_SCOPE_CLOSE: break;

            case VMOP_YIELD: break;

            case VMOP_PRINT_STACK: print_ltvs(stdout,CODE_BLUE,LTV_list(vm_res_deq(env,VMRES_STACK,KEEP)),CODE_RESET "\n",0); break;
            case VMOP_GRAPH_STACK: graph_ltvs_to_file("/tmp/jj.dot",LTV_list(vm_res_deq(env,VMRES_STACK,KEEP)),0,"tos"); break;
            case VMOP_PRINT_REF:   REF_printall(stdout,LTV_list(vm_res_deq(env,VMRES_REF,KEEP)),"Ref: "); break;
            case VMOP_GRAPH_REF:   REF_dot(stdout,LTV_list(vm_res_deq(env,VMRES_REF,KEEP)),"Ref: "); break;
            default:
                break;
        }
    }


    return state;
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
    { VMOP_RDLOCK },
    { VMOP_LTV,-1,LT_DUP,"test item 1" },
    { VMOP_PRINT_STACK },
    { VMOP_LTV,-1,LT_DUP,"test item 2" },
    { VMOP_PRINT_STACK },
    { VMOP_LTV,-1,LT_DUP,"test item 3" },
    { VMOP_PRINT_STACK },
    { VMOP_LTV,-1,LT_DUP,"test item 4" },
    { VMOP_PRINT_STACK },
    { VMOP_DUP },
    { VMOP_PRINT_STACK },
    { VMOP_UNLOCK },
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
