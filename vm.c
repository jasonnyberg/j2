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
#include "extensions.h"

pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

enum {
    VMRES_DICT,
    VMRES_STACK,
    VMRES_FUNC,
    VMRES_CTXT, // bypass/throw
    VMRES_CODE,
    VMRES_COUNT,
} VM_LISTRES;

static char *res_name[] = { "VMRES_DICT","VMRES_STACK","VMRES_FUNC","VMRES_CTXT","VMRES_CODE" };

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
static LTV *unstack_bc=NULL;

__attribute__((constructor))
static void init(void)
{
    VM_CMD unstack_asm[] = {{VMOP_RESET},{VMOP_CTX_POP},{VMOP_REMOVE}};
    vm_ltv_container=LTV_NULL_LIST;
    unstack_bc=compile(compilers[FORMAT_asm],unstack_asm,3);
    LTV_put(LTV_list(vm_ltv_container),unstack_bc,HEAD,NULL);
}


//////////////////////////////////////////////////
void debug(const char *fromwhere) { return; }
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Basic Utillties

#define ENV_LIST(res) LTV_list(&vm_env->ltv[res])

static LTV *vm_enq(int res,LTV *tos) { return LTV_put(ENV_LIST(res),tos,HEAD,NULL); }
static LTV *vm_deq(int res,int pop)  { return LTV_get(ENV_LIST(res),pop,HEAD,NULL,NULL); }

static LTV *vm_stack_enq(LTV *ltv) { return LTV_enq(LTV_list(vm_deq(VMRES_STACK,KEEP)),ltv,HEAD); }
static LTV *vm_stack_deq(int pop) {
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
    vm_enq(VMRES_CTXT,(ltv));
    vm_reset_ext();
}

//////////////////////////////////////////////////
// Specialized Utillties

static LTV *vm_use_ext() {
    if (vm_env->ext_data && !vm_env->ext)
        vm_env->ext=LTV_init(NEW(LTV),vm_env->ext_data,vm_env->ext_length,vm_env->ext_flags);
    return vm_env->ext;
}

static void vm_push_code(LTV *ltv) {
    THROW(!ltv,LTV_NULL);
    LTV *opcode_ltv=LTV_init(NEW(LTV),ltv->data,ltv->len,LT_BIN|LT_LIST|LT_BC);
    THROW(!opcode_ltv,LTV_NULL);
    DEBUG(fprintf(stderr,CODE_RED "  CODE %x" CODE_RESET "\n",opcode_ltv->data));
    THROW(!LTV_enq(LTV_list(opcode_ltv),ltv,HEAD),LTV_NULL); // encaps code ltv within tracking ltv
    THROW(!vm_enq(VMRES_CODE,opcode_ltv),LTV_NULL);
 done:
    return;
}

static void vm_get_code() {
    vm_env->code_ltvr=NULL;
    THROW(!(vm_env->code_ltv=LTV_get(ENV_LIST(VMRES_CODE),KEEP,HEAD,NULL,&vm_env->code_ltvr)),LTV_NULL);
    DEBUG(fprintf(stderr,CODE_RED "  GET CODE %x" CODE_RESET "\n",vm_env->code_ltv->data));
 done: return;
}

static void vm_listcat(int res) { // merge tos into head of nos
    LTV *tos,*nos;
    THROW(!(tos=vm_deq(res,POP)),LTV_NULL);
    THROW(!(nos=vm_deq(res,KEEP)),LTV_NULL);
    THROW(!(tos->flags&LT_LIST && nos->flags&LT_LIST),LTV_NULL);
    CLL_MERGE(LTV_list(nos),LTV_list(tos),HEAD);
    LTV_release(tos);
 done:
    return;
}

static void vm_pop_code() { DEBUG(fprintf(stderr,CODE_RED "  CODE %x" CODE_RESET "\n",vm_env->code_ltv->data));
    if (vm_env->code_ltvr)
        LTVR_release(&vm_env->code_ltvr->lnk);
    if (CLL_EMPTY(ENV_LIST(VMRES_CODE)))
        vm_env->state|=VM_COMPLETE;
}

static void vm_resolve(CLL *cll,LTV *ref) {
    int status=0;
    LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
    void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
    CLL_map(cll,FWD,op);
 done:
    return;
}

void vm_dump_ltv(LTV *ltv,char *label) {
    char *filename;
    printf("%s\n",label);
    print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0);
    graph_ltv_to_file(FORMATA(filename,32,"/tmp/%s.dot",label),ltv,0,label);
 done:
    return;
}

static void vm_ffi(LTV *lambda) { // adapted from edict.c's ffi_eval(...)
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
        STRY(!(coerced=cif_coerce_i2c(arg,type)),"coercing ffi arg (%s)",name);
        LTV_enq(&args,coerced,HEAD); // enq coerced arg onto args CLL
        LT_put(rval,name,HEAD,coerced); // coerced args are installed as childen of rval
        LTV_release(arg);
    done:
        return status;
    }
    THROW(cif_args_marshal(ftype,REV,marshaller),LTV_NULL); // pre-
    THROW(cif_ffi_call(ftype,lambda->data,rval,&args),LTV_NULL);
    if (void_func)
        LTV_release(rval);
    else
        THROW(!vm_stack_enq(cif_coerce_c2i(rval)),LTV_NULL);
    CLL_release(&args,LTVR_release); //  ALWAYS release at end, to give other code a chance to enq an LTV
 done:
    return;
}

static void vm_eval_type(LTV *type) {
    vm_reset_ext(); // sanitize
    if (type->flags&LT_TYPE) {
        LTV *cvar;
        THROW(!(cvar=cif_create_cvar(type,NULL,NULL)),LTV_NULL);
        THROW(!vm_stack_enq(cvar),LTV_NULL);
    } else
        vm_ffi(type); // if not a type, it could be a function
    vm_reset_ext(); // sanitize again
 done:
    return;
}

static void vm_eval_ltv(LTV *ltv) {
    THROW(!ltv,LTV_NULL);
    if (ltv->flags&LT_CVAR) // type, ffi, ...
        vm_eval_type(ltv);
    else
        vm_push_code(compile_ltv(compilers[FORMAT_edict],ltv));
    vm_env->state|=VM_YIELD;
 done: return;
}

extern void is_lit() {
    LTV *ltv;
    THROW(!(ltv=vm_stack_deq(POP)),LTV_NULL); // ,"popping ltv to split");
    THROW(ltv->flags&LT_NSTR,LTV_NULL); // throw if non-string
    int tlen=series(ltv->data,ltv->len,WHITESPACE,NULL,NULL);
    THROW(tlen&&ltv->len==tlen,LTV_NULL);
 done:
    return;
}

extern void split() {
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
    return;
}

extern void dump() {
    vm_dump_ltv(&vm_env->ltv[VMRES_DICT],res_name[VMRES_DICT]);
    vm_dump_ltv(&vm_env->ltv[VMRES_STACK],res_name[VMRES_STACK]);
    return;
}

extern void locals() {
    int old_show_ref=show_ref;
    show_ref=1;
    vm_dump_ltv(vm_deq(VMRES_DICT,KEEP),res_name[VMRES_DICT]);
    vm_dump_ltv(vm_deq(VMRES_STACK,KEEP),res_name[VMRES_STACK]);
    show_ref=old_show_ref;
    return;
}

extern void stack() {
    int old_show_ref=show_ref;
    show_ref=1;
    vm_dump_ltv(vm_deq(VMRES_STACK,KEEP),res_name[VMRES_STACK]);
    show_ref=old_show_ref;
    return;
}

extern LTV *encaps_ltv(LTV *ltv) {
    LTV *ltvltv=NULL;
    THROW(!(ltvltv=cif_create_cvar(cif_type_info("(LTV)*"),NULL,NULL)),LTV_NULL); // allocate an LTV *
    (*(LTV **) ltvltv->data)=ltv; // ltvltv->data is a pointer to an LTV *
    THROW(!(LT_put(ltvltv,"TYPE_CAST",HEAD,ltv)),LTV_NULL);
 done:
    return ltvltv;
}

extern void encaps() {
    THROW(!(vm_stack_enq(encaps_ltv(vm_stack_deq(POP)))),LTV_NULL); // ,"pushing encaps ltv cvar");
 done:
    return;
}

extern LTV *decaps_ltv(LTV *ltv) { return ltv; } // all the work is done via coersion

extern void decaps() {
    LTV *cvar_ltv=NULL,*ptr=NULL;
    THROW(!(cvar_ltv=vm_stack_deq(POP)),LTV_NULL);
    THROW(!(cvar_ltv->flags&LT_CVAR),LTV_NULL); // "checking at least if it's a cvar" // TODO: verify it's an "(LTV)*"
    THROW(!(ptr=*(LTV **) (cvar_ltv->data)),LTV_NULL);
    THROW(!(vm_stack_enq(ptr)),LTV_NULL);
 done:
    LTV_release(cvar_ltv);
    return;
}

extern void vm_while(LTV *lambda) {
    while (!vm_env->state)
        vm_eval_ltv(lambda);
}

extern void dup() {
    THROW(!vm_stack_enq(vm_stack_deq(KEEP)),LTV_NULL);
 done: return;
}

//////////////////////////////////////////////////
// Opcode Handlers
//////////////////////////////////////////////////

#define VMOP_DEBUG() DEBUG(debug(__func__); fprintf(stderr,"%d %d %s\n",vm_env->state,vm_env->skipdepth,__func__));
#define SKIP_IF_STATE() do { if (vm_env->state) { DEBUG(fprintf(stderr,"  (skipping %s)\n",__func__)); goto done; }} while(0);

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
    else if (vm_env->state|VM_THROWING && (!vm_env->skipdepth)) {
        LTV *exception=NULL;
        STRY(!(exception=vm_deq(VMRES_CTXT,KEEP)),"peeking at exception stack");
        if (vm_use_ext()) {
            vm_env->ext=REF_create(vm_env->ext);
            vm_resolve(ENV_LIST(VMRES_DICT),vm_env->ext);
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

static void vmop_PUSHEXT() { VMOP_DEBUG();
    SKIP_IF_STATE();
    if (vm_use_ext())
        vm_stack_enq(vm_env->ext);
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
    vm_resolve(ENV_LIST(VMRES_DICT),vm_use_ext());
    LTV *ltv=REF_ltv(REF_HEAD(vm_env->ext));
    if (!ltv)
        ltv=LTV_dup(vm_env->ext);
    THROW(!vm_stack_enq(ltv),LTV_NULL);
 done: return;
}

static void vmop_ASSIGN() { VMOP_DEBUG();
    SKIP_IF_STATE();
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

static void vmop_REMOVE() { VMOP_DEBUG();
    SKIP_IF_STATE();
    if (vm_use_ext()) {
        vm_resolve(ENV_LIST(VMRES_DICT),vm_env->ext);
        REF_remove(REF_HEAD(vm_env->ext));
    } else {
        LTV_release(vm_stack_deq(POP));
    }
 done: return;
}

#define SKIP vm_env->skipdepth++
#define DESKIP do { if (vm_env->skipdepth>0) vm_env->skipdepth--; } while (0)

static void vmop_CTX_PUSH() { VMOP_DEBUG();
    if (vm_env->state) { DEBUG(fprintf(stderr,"  (skipping %s)\n",__func__));
        SKIP;
    } else {
        THROW(!vm_enq(VMRES_DICT,vm_stack_deq(POP)),LTV_NULL);
        THROW(!vm_enq(VMRES_STACK,LTV_NULL_LIST),LTV_NULL);
    }
 done: return;
}

static void vmop_CTX_POP() { VMOP_DEBUG();
    if (vm_env->state && vm_env->skipdepth) { DEBUG(fprintf(stderr,"  (skipping %s)\n",__func__));
        DESKIP;
    } else {
        vm_listcat(VMRES_STACK);
        THROW(!vm_stack_enq(vm_deq(VMRES_DICT,POP)),LTV_NULL);
    }
 done: return;
}

static void vmop_FUN_PUSH() { VMOP_DEBUG();
    if (vm_env->state) { DEBUG(fprintf(stderr,"  (skipping %s)\n",__func__));
        SKIP;
    } else {
        THROW(!vm_enq(VMRES_DICT,LTV_NULL),LTV_NULL);
        THROW(!vm_enq(VMRES_STACK,LTV_NULL_LIST),LTV_NULL);
        THROW(!vm_enq(VMRES_FUNC,vm_stack_deq(POP)),LTV_NULL);
    }
 done: return;
}

static void vmop_FUN_EVAL() { VMOP_DEBUG();
    if (vm_env->state && vm_env->skipdepth) { DEBUG(fprintf(stderr,"  (skipping %s)\n",__func__));
        DESKIP;
    } else {
        vm_push_code(unstack_bc); // stack signal to CTX_POP and REMOVE after eval
        vm_eval_ltv(vm_deq(VMRES_FUNC,POP));
    }
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
    DEBUG(fprintf(stderr,CODE_BLUE " ");
          fprintf(stderr,"len %d flags %x state %x; -----> ",vm_env->ext_length,vm_env->ext_flags,vm_env->state);
          fstrnprint(stderr,vm_env->ext_data,vm_env->ext_length);
          fprintf(stderr,CODE_RESET "\n"));
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
    vmop_FUN_EVAL
};

//////////////////////////////////////////////////
// Opcode Dispatch

static VMOP_CALL vm_dispatch(VMOP_CALL call) {
    ADVANCE(1);
    call();
    if (vm_env->code_ltv->len<=0) {
        vm_env->state|=VM_YIELD;
        vm_env->state&=~VM_BYPASS;
        vm_pop_code();
    }
    return vm_env->state?NULL:vmop_call[OPCODE];
}

static int vm_run() {
    vm_push_code(compile(compilers[FORMAT_edict],"CODE!",-1));
    while (!(vm_env->state&(VM_COMPLETE|VM_ERROR))) {
        vm_reset_ext();
        vm_get_code();
        VMOP_CALL call=vmop_call[OPCODE];
        do {
            call=vm_dispatch(call);
        } while (call);
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

char *vm_interpreter=
    "[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl\n"
    "ROOT<repl([bootstrap.edict] [r] file_open!) ARG0 decaps! <> encaps! RETURN @>";

extern int vm_interpret() {
    try_depth=0;
    LTV *rval=vm_eval(cif_module,LTV_init(NEW(LTV),vm_interpreter,-1,LT_NONE),LTV_NULL);
    if (rval)
        print_ltv(stdout,"",rval,"\n",0);
    return !rval;
}
