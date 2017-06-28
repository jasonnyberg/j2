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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h> // basename

#include "cll.h"

#define _GNU_SOURCE
#define _C99
#include <stdlib.h>
#include "util.h"
#include "listree.h"
#include "reflect.h"

#include "trace.h" // lttng


//////////////////////////////////////////////////
// Utils
//////////////////////////////////////////////////

LTV *push(CLL *cll,LTV *ltv) { return LTV_enq((cll),(ltv),HEAD); }
LTV *pop(CLL *cll)           { LTV_deq((cll),HEAD); }
LTV *peek(CLL *cll)          { LTV_peek((cll),HEAD); }

//////////////////////////////////////////////////

typedef struct EDICT
{
    LTV *root;
    CLL threads;
} EDICT;

//////////////////////////////////////////////////

enum { EVAL_SUCCESS=0, EVAL_ITER };

enum {
    DEBUG_FILE     = 1<<0,
    DEBUG_ATOM     = 1<<1,
    DEBUG_EXPR     = 1<<2,
    DEBUG_BAIL     = 1<<3,
    DEBUG_PREEVAL  = 1<<4,
    DEBUG_POSTEVAL = 1<<5,
    DEBUG_ERR      = 1<<6
};

int debug_dump=0;
int debug=DEBUG_BAIL|DEBUG_ERR;

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

int tok_count=0;

typedef enum {
    TOK_NONE =0,
    TOK_POP  =1<<0,
    TOK_FILE =1<<1,
    TOK_EXPR =1<<2,
    TOK_LIT  =1<<3,
    TOK_ATOM =1<<4,
    TOK_REF  =1<<5,
    TOK_FFI  =1<<6,
} TOK_FLAGS;

typedef struct TOK {
    CLL lnk;
    CLL ltvs;
    CLL lambdas;
    CLL children;
    TOK_FLAGS flags;
} TOK;

//////////////////////////////////////////////////
// REPL Thread
//////////////////////////////////////////////////

typedef struct THREAD {
    CLL lnk;
    EDICT *edict;
    CLL dict;        // cll of ltvr
    CLL toks;        // cll of tok
    CLL exceptions;
    int skipdepth;
    int skip;
} THREAD;

THREAD *THREAD_new();
void THREAD_free(THREAD *thread);

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

TOK *toks_push(CLL *cll,TOK *tok) { return (TOK *) CLL_put(cll,&tok->lnk,HEAD); }
TOK *toks_pop(CLL *cll)           { return (TOK *) CLL_get(cll,POP,HEAD);       }
TOK *toks_peek(CLL *cll)          { return (TOK *) CLL_get(cll,KEEP,HEAD);      }

LTV *tok_push(TOK *tok,LTV *ltv) { return push(&tok->ltvs,ltv); }
LTV *tok_pop(TOK *tok )          { return pop(&tok->ltvs);      }
LTV *tok_peek(TOK *tok)          { return peek(&tok->ltvs);     }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    TOK *tok=NULL;
    if (ltv && (tok=NEW(TOK)))
    {
        CLL_init(&tok->lnk);
        CLL_init(&tok->ltvs);
        CLL_init(&tok->lambdas);
        CLL_init(&tok->children);
        tok->flags=flags;
        tok_push(tok,ltv);
        tok_count++;
    }
    return tok;
}

TOK *TOK_cut(TOK *tok) { return tok?(TOK *)CLL_cut(&tok->lnk):NULL; } // take it out of any list it's in

void TOK_free(TOK *tok)
{
    void TOK_release(CLL *lnk) { TOK_free((TOK *) lnk); }

    if (!tok) return;
    TOK_cut(tok);
    CLL_release(&tok->ltvs,LTVR_release);
    CLL_release(&tok->lambdas,LTVR_release);
    if (tok->flags&TOK_REF) // ref tok children are LT REFs
        REF_delete(&tok->children);
    else
        CLL_release(&tok->children,TOK_release);

    RELEASE(tok);
    tok_count--;
}

TOK *TOK_expr(char *buf,int len) { return TOK_new(TOK_EXPR,LTV_init(NEW(LTV),buf,len,LT_NONE)); } // ownership of buf is external

void show_tok_flags(FILE *ofile,TOK *tok)
{
    if (tok->flags&TOK_POP)     fprintf(ofile,"POP ");
    if (tok->flags&TOK_FILE)    fprintf(ofile,"FILE ");
    if (tok->flags&TOK_EXPR)    fprintf(ofile,"EXPR ");
    if (tok->flags&TOK_LIT)     fprintf(ofile,"LIT ");
    if (tok->flags&TOK_ATOM)    fprintf(ofile,"ATOM ");
    if (tok->flags&TOK_REF)     fprintf(ofile,"REF ");
    if (tok->flags&TOK_FFI)     fprintf(ofile,"FFI ");
}

void show_toks(FILE *ofile,char *pre,CLL *toks,char *post);

void show_tok(FILE *ofile,char *pre,TOK *tok,char *post) {
    if (pre) fprintf(ofile,"%s",pre);
    show_tok_flags(ofile,tok);
    print_ltvs(ofile,"",&tok->ltvs,"",1);
    print_ltvs(ofile,"",&tok->lambdas,"",1);
    if (tok->flags&TOK_REF)
        REF_printall(ofile,&tok->children,"Refs: ");
    else
        show_toks(ofile," (",&tok->children,")");
    if (post) fprintf(ofile,"%s",post);
    fflush(ofile);
}

void show_toks(FILE *ofile,char *pre,CLL *toks,char *post)
{
    void *op(CLL *lnk) { show_tok(ofile,"",(TOK *) lnk,""); return NULL; }
    if (toks) {
        if (pre) fprintf(ofile,"%s",pre);
        CLL_map(toks,FWD,op);
        if (post) fprintf(ofile,"%s",post);
    }
    fflush(ofile);
}

//////////////////////////////////////////////////
// REPL Thread
//////////////////////////////////////////////////

CLL *dict(THREAD *thread) { return thread?&thread->dict:NULL; }
CLL *toks(THREAD *thread) { return thread?&thread->toks:NULL; }
CLL *exceptions(THREAD *thread) { return thread?&thread->exceptions:NULL; }

//////////////////////////////////////////////////

TOK *reftok_init(LTV *ltv)
{
    TOK *ref_tok=TOK_new(TOK_REF,ltv);
    REF_create(ltv->data,ltv->len,&ref_tok->children);
    return ref_tok;
}

int reftok_put(LTV *root,TOK *ref_tok,LTV *ltv)
{
    CLL *ref=&ref_tok->children;
    REF *ref_head=REF_HEAD(ref);
    REF_resolve(root,ref,true);
    REF_assign(ref_head,ltv);
    REF_reset(ref_head,NULL);
    return 0;
}

LTV *reftok_get(LTV *root,TOK *ref_tok,int pop)
{
    CLL *ref=&ref_tok->children;
    REF *ref_head=REF_HEAD(ref);
    REF_resolve(root,ref,true);
    LTV *ltv=REF_ltv(ref_head);
    if (pop && ltv)
        REF_remove(ref_head);
    REF_reset(ref_head,NULL);
    return ltv;
}

//////////////////////////////////////////////////

LTV *stack_put(THREAD *thread,LTV *ltv) { return LT_put(peek(dict(thread)),"$",HEAD,ltv); }
LTV *stack_get(THREAD *thread,int pop) {
    void *stack_resolve(CLL *lnk) { return LT_get(((LTVR *) lnk)->ltv,"$",HEAD,pop); }
    return CLL_map(dict(thread),FWD,stack_resolve);
}

int exception(THREAD *thread,LTV *ltv) { return !push(exceptions(thread),ltv); }

//////////////////////////////////////////////////

THREAD *THREAD_new(EDICT *edict)
{
    int status=0;
    THREAD *thread=NULL;

    STRY(!(thread=NEW(THREAD)),"allocating thread");
    TRYCATCH(!(thread->edict=edict),-1,release_thread,"assigning thread's edict");
    CLL_init(dict(thread));
    CLL_init(toks(thread));
    CLL_init(exceptions(thread));
    thread->skipdepth=0;
    thread->skip=false;

    TRYCATCH(!push(dict(thread),edict->root),-1,release_thread,"pushing thread->dict root");
    TRYCATCH(!CLL_put(&edict->threads,&thread->lnk,HEAD),-1,release_thread,"pushing thread into edict");

    goto done; // success!

 release_thread:
    THREAD_free(thread);
    thread=NULL;

 done:
    return thread;
}

void THREAD_free(THREAD *thread)
{
    if (!thread) return;
    CLL_cut(&thread->lnk); // remove from any list it's in
    //CLL_release(dict(thread),LTVR_release);
    CLL_release(exceptions(thread),LTVR_release);
    void tok_free(CLL *lnk) { TOK_free((TOK *) lnk); }
    CLL_release(toks(thread),tok_free);
    RELEASE(thread);
}

//////////////////////////////////////////////////
// instrumentation
//////////////////////////////////////////////////

int edict_graph(FILE *ofile,EDICT *edict)
{
    int status=0;

    void descend_toks(CLL *toks,char *label) {
        void *op(CLL *lnk) {
            TOK *tok=(TOK *) lnk;
            fprintf(ofile,"\"%x\" [shape=box style=filled fillcolor=green4 label=\"",tok);
            show_tok_flags(ofile,tok);
            fprintf(ofile,"\"]\n\"%x\" -> \"%x\" [color=darkgreen penwidth=2.0]\n",tok,lnk->lnk[0]);
            fprintf(ofile,"\"%2$x\"\n\"%1$x\" -> \"%2$x\"\n",tok,&tok->ltvs);
            ltvs2dot(ofile,&tok->ltvs,0,NULL);
            if (!CLL_EMPTY(&tok->lambdas)) {
                fprintf(ofile,"\"%2$x\"\n\"%1$x\" -> \"%2$x\" [label=\"&#955;\"]\n",tok,&tok->lambdas);
                ltvs2dot(ofile,&tok->lambdas,0,NULL);
            }
            if (CLL_HEAD(&tok->children)) {
                fprintf(ofile,"\"%x\" -> \"%x\"\n",tok,&tok->children);
                if (tok->flags&TOK_REF)
                    REF_dot(ofile,&tok->children,"Refs");
                else
                    descend_toks(&tok->children,"Subtoks");
            }
        }

        fprintf(ofile,"\"%x\" [shape=ellipse label=\"%s\"]\n",toks,label);
        fprintf(ofile,"\"%x\" -> \"%x\" [color=darkgreen penwidth=2.0]\n",toks,toks->lnk[0]);
        CLL_map(toks,FWD,op);
    }

    void show_thread(THREAD *thread) {
        int halt=0;
        fprintf(ofile,"\"Thread %x\"\n",thread);
        fprintf(ofile,"\"D%2$x\" [label=\"Dict\"]\n\"Thread %1$x\" -> \"D%2$x\" -> \"%2$x\"\n",thread,dict(thread));
        ltvs2dot(ofile,dict(thread),0,NULL);
        fprintf(ofile,"\"Thread %x\" -> \"%x\"\n",thread,toks(thread));
        descend_toks(toks(thread),"Toks");
    }

    void show_threads(char *pre,CLL *threads,char *post) {
        void *op(CLL *lnk) { show_thread((THREAD *) lnk); return NULL; }
        if (pre) fprintf(ofile,"%s",pre);
        CLL_map(threads,FWD,op);
        if (post) fprintf(ofile,"%s",post);
    }

    if (!edict) goto done;

    fprintf(ofile,"digraph iftree\n{\ngraph [/*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");
    fprintf(ofile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    show_threads("",&edict->threads,"\n");
    fprintf(ofile,"}\n");

 done:
    return status;
}

int edict_graph_to_file(char *filename,EDICT *edict)
{
    int status=0;
    FILE *ofile=NULL;
    STRY(!(ofile=fopen(filename,"w")),"opening %s for writing",filename);
    TRYCATCH(edict_graph(ofile,edict),status,close_file,"graphing edict to %s",filename);
 close_file:
    fclose(ofile);
 done:
    return status;
}



//////////////////////////////////////////////////
// tokenizer
//////////////////////////////////////////////////

#define EDICT_OPS "|&!%#@/+="
#define EDICT_MONO_OPS "()<>{}"

int tokenize(TOK *tok)
{
    int status=0;
    char *data=NULL;
    int len=0,tlen=0;
    LTV *tokval=NULL;

    int advance(int bump) { bump=MIN(bump,len); data+=bump; len-=bump; return bump; }

    TOK *append(TOK *tok,int type,char *data,int len,int adv) {
        TOK *subtok=TOK_new(type,LTV_init(NEW(LTV),data,len,type==TOK_LIT?LT_DUP:LT_NONE)); // only LITs need to be duped
        if (!subtok) return NULL;
        advance(adv);
        return (TOK *) CLL_put(&tok->children,&subtok->lnk,TAIL);
    }

    STRY(!tok,"testing for null tok");
    STRY(!(tokval=tok_peek(tok)),"testing for tok ltvr value");

    if (tokval->flags&LT_CVAR) {
        TOK *subtok=TOK_new(TOK_FFI,tokval);
        STRY(!CLL_put(&tok->children,&subtok->lnk,TAIL),"appending FFI tok");
        goto done;
    }

    STRY(tokval->flags&LT_NSTR,"testing for non-string tok ltvr value");
    STRY(!tokval->data,"testing for null tok ltvr data");
    data=tokval->data;
    len=tokval->len;

    while (len) {
        if (tlen=series(data,len,WHITESPACE,NULL,NULL)) // whitespace
            advance(tlen); // TODO: A) embed WS tokens, B) stash tail WS token at start of ops (discard intermediates), C) reinsert WS when ops done
        else if (tlen=series(data,len,NULL,NULL,"[]")) // lit
            STRY(!append(tok,TOK_LIT,data+1,tlen-2,tlen),"appending lit");
        else if (tlen=series(data,len,EDICT_MONO_OPS,NULL,NULL)) // special, non-ganging op
            STRY(!append(tok,TOK_ATOM,data,1,1),"appending %c",*data);
        else { // ANYTHING else is ops and/or ref
            TOK *ops=NULL;
            tlen=series(data,len,EDICT_OPS,NULL,NULL); // ops
            STRY(!(ops=append(tok,TOK_ATOM,data,tlen,tlen)),"appending ops");
            if ((tlen=series(data,len,NULL,WHITESPACE EDICT_OPS EDICT_MONO_OPS,"[]"))) // ref
                STRY(!append(ops,TOK_REF,data,tlen,tlen),"appending ref");
        }
    }

 done:
    return status;
}

//////////////////////////////////////////////////
// eval engine
//////////////////////////////////////////////////

// push expr onto tok stack, tokenizing if necessary
int eval_push(THREAD *thread,TOK *tok) { // engine pops
    int status=0;
    if (tok && tok->flags&TOK_EXPR && CLL_EMPTY(&tok->children))
        STRY(tokenize(tok),"tokenizing expr");
    STRY(!toks_push(toks(thread),tok),"pushing tok to eval stack");
 done:
    return status;
}

// walk up stack of lexical scopes looking for reference; inserts always resolve at most-local scope
int edict_resolve(THREAD *thread,CLL *ref,int insert) {
    int status=0;
    STRY(!thread || !ref,"validating args");
    void *dict_resolve(CLL *lnk) { return REF_resolve(((LTVR *) lnk)->ltv,ref,insert)?NULL:NON_NULL; } // if lookup failed, continue map by returning NULL
    status=!CLL_map(dict(thread),FWD,dict_resolve);
 done:
    return status;
}

// solo ref, i.e. not part of an atom; must have lambda attached
int ref_eval(THREAD *thread,TOK *ref_tok)
{
    int status=0;
    REF *ref_head=NULL;
    LTV *lambda_ltv=NULL,*ref_ltv=NULL;

    STRY(!(lambda_ltv=peek(&ref_tok->lambdas)),"validating ref lambda");
    STRY(!(ref_head=REF_HEAD(&ref_tok->children)),"validating ref head");
    STRY(!(ref_ltv=REF_ltv(ref_head)),"validating deref result");
    STRY(!stack_put(thread,ref_ltv),"pushing resolved ref to stack");
    STRY(eval_push(thread,TOK_new(TOK_EXPR,lambda_ltv)),"pushing lambda expr");
    TRYCATCH(REF_iterate(&ref_tok->children,ref_tok->flags&TOK_POP),0,terminate,"iterating ref");
    TRYCATCH(!REF_ltv(ref_head),0,terminate,"iterating ref");
    goto done; // success!

 terminate:
    TOK_free(ref_tok);
 done:
    return status;
}

// ops and/or ref
int atom_eval(THREAD *thread,TOK *ops_tok) // ops contains refs in children
{
    int status=0;
    LTV *ltv=NULL; // optok's data

    STRY(!(ltv=tok_peek(ops_tok)),"getting optok data");
    char *ops=(char *) ltv->data;
    int opslen=ltv->len;

    int assignment=series(ops,opslen,NULL,"@",NULL)<opslen; // ops contains "@", i.e. assignment
    int wildcard=0;

    TOK *ref_tok=NULL;
    REF *ref_head=NULL;

    int throw(LTV *ltv) {
        int status=0;
        if (ltv) {
            STRY(exception(thread,ltv),"throwing exception");
        }
        else if (ref_head) {
            TRYCATCH(edict_resolve(thread,&ref_tok->children,false),0,done,"resolving ref for throw");
            STRY(exception(thread,REF_ltv(ref_head)),"throwing named exception");
        } else {
            STRY(exception(thread,stack_get(thread,POP)),"throwing exception from stack");
        }
    done:
        return status;
    }

    int catch() {
        int status=0;
        LTV *exception=NULL;
        STRY(!(exception=peek(exceptions(thread))),"peeking at exception stack");
        if (ref_head) { // looking for a specific exception to catch
            TRYCATCH(edict_resolve(thread,&ref_tok->children,false),0,done,"resolving ref for catch");
            if (exception!=REF_ltv(ref_head))
                goto done;
        }
        LTV_release(pop(exceptions(thread)));
    done:
        return status;
    }

    int special() { // handle special operations
        int readfrom()  {
            int status=0;
            LTV *ltv_ifilename=NULL;
            STRY(!(ltv_ifilename=stack_get(thread,POP)),"popping import filename");
            char *ifilename=bufdup(ltv_ifilename->data,ltv_ifilename->len);
            FILE *ifile=strncmp("stdin",ifilename,5)?fopen(ifilename,"r"):stdin;
            if (ifile) {
                TOK *file_tok=TOK_new(TOK_FILE,LTV_init(NEW(LTV),(void *) ifile,sizeof(FILE *),LT_IMM));
                STRY(eval_push(thread,file_tok),"pushing file");
            }
            myfree(ifilename,strlen(ifilename)+1);
            LTV_release(ltv_ifilename);
        done:
            return status;
        }

        int import(int bootstrap) {
            int status=0;
            LTV *mod_ltv=NULL;
            STRY(!(mod_ltv=stack_get(thread,KEEP)),"peeking dwarf import filename");
            STRY(ref_curate_module(mod_ltv,bootstrap),"importing module");
        done:
            return status;
        }

        int preview() {
            int status=0;
            LTV *mod_ltv=NULL;
            STRY(!(mod_ltv=stack_get(thread,KEEP)),"peeking dwarf import filename");
            STRY(ref_preview_module(mod_ltv),"listing module");
        done:
            return status;
        }

        int cvar() {
            int status=0;
            LTV *type,*cvar;
            STRY(!(type=stack_get(thread,POP)),"popping type");
            STRY(!(cvar=ref_create_cvar(type,NULL,NULL)),"creating cvar");
            STRY(!stack_put(thread,cvar),"pushing cvar");
        done:
            return status;
        }

        int ffi() {
            int status=0;
            LTV *type,*cvar;
            STRY(!(type=stack_get(thread,KEEP)),"popping type");
            STRY(ref_ffi_prep(type),"prepping cvar for ffi");
        done:
            return status;
        }

        int dump(char *label) {
            int status=0;
            edict_resolve(thread,&ref_tok->children,false);
            LTV *cvar=NULL;
            LTI *lti=NULL;
            if ((lti=REF_lti(ref_head))) {
                CLL *ltvs=&lti->ltvs;
                graph_ltvs_to_file("/tmp/jj.dot",ltvs,0,label);
                print_ltvs(stdout,CODE_BLUE,ltvs,CODE_RESET "\n",2);
            }
            else if ((cvar=REF_ltv(ref_head)) && cvar->flags&LT_CVAR)
                ref_print_cvar(stdout,cvar,0);
        done:
            return status;
        }

        int dup() { return !stack_put(thread,LTV_dup(stack_get(thread,KEEP))); }

        int ro(int is_ro) {
            int status=0;
            LTV *ltv=NULL;
            STRY(!(ltv=stack_get(thread,KEEP)),"getting tos");
            if (is_ro) ltv->flags|=LT_RO;
            else       ltv->flags&=~LT_RO;
        done:
            return status;
        }

        int status=0;
        if (ref_head) {
            LTV *key=REF_key(ref_head);
            char *buf=NULL;
#define BUILTIN(cmd) (!strnncmp(key->data,key->len,(#cmd),-1))
            if (key) {
                if      BUILTIN(read)      STRY(readfrom(),"evaluating #read");
                else if BUILTIN(preview)   STRY(preview(),"evaluating #preview");
                else if BUILTIN(bootstrap) STRY(import(true),"evaluating #bootstrap");
                else if BUILTIN(import)    STRY(import(false),"evaluating #import");
                else if BUILTIN(ffi)       STRY(ffi(),"evaluating #ffi");
                else if BUILTIN(new)       STRY(cvar(),"evaluating #new");
                else if BUILTIN(dup)       STRY(dup(),"evaluating #dup");
                else if BUILTIN(ro)        STRY(ro(true),"evaluating #ro");
                else if BUILTIN(rw)        STRY(ro(false),"evaluating #rw");
                else STRY(dump(PRINTA(buf,key->len,(char *) key->data)),"dumping named item");
            }
        } else {
            edict_graph_to_file("/tmp/jj.dot",thread->edict);
        }

    done:
        return status;
    }

    int deref() {
        int status=0;
        if (edict_resolve(thread,&ref_tok->children,false) || !REF_ltv(ref_head)) // if lookup failed, push copy to stack
            STRY(!stack_put(thread,LTV_dup(tok_peek(ref_tok))),"pushing unresolved ref (implicit lit) back to stack");
        else
            STRY(!stack_put(thread,REF_ltv(ref_head)),"pushing resolved ref to stack");
    done:
        return status;
    }

    int assign() { // resolve refs needs to not worry about last ltv, just the lti is important.
        int status=0;
        LTV *tos=NULL;
        TRYCATCH(edict_resolve(thread,&ref_tok->children,true),0,exception,"resolving ref for assign");
        TRYCATCH(!(tos=stack_get(thread,KEEP)),0,exception,"peeking anon");
        TRYCATCH(REF_assign(ref_head,tos),0,exception,"assigning anon to ref");
        stack_get(thread,POP); // succeeded, detach anon from stack
        goto done;

    exception:
        TOK_free(ref_tok);
        throw(LTV_init(NEW(LTV),"exception: assign",-1,0));
    done:
        return status;
    }

    int remove() {
        int status=0;
        if (ref_head) {
            TRYCATCH(edict_resolve(thread,&ref_tok->children,false),0,done,"resolving ref for remove");
            STRY(REF_remove(ref_head),"performing ref remove");
        } else {
            LTV_release(stack_get(thread,POP));
        }
    done:
        return status;
    }

    int scope_open() {
        int status=0;
        LTV *scope=NULL;
        STRY(!(scope=stack_get(thread,POP)),"popping scope from stack");
        STRY(!push(dict(thread),scope),"pushing new local stack");
    done:
        return status;
    }

    int scope_close() {
        int status=0;
        LTV *oldscope=NULL,*newscope=NULL;
        LTI *oldstack=NULL,*newstack=NULL;
        STRY(!(oldscope=pop(dict(thread))),"popping old scope");
        STRY(!(newscope=peek(dict(thread))),"peeking new scope");
        STRY(!(oldstack=LTI_resolve(oldscope,"$",false)),"resolving old stack");
        STRY(!(newstack=LTI_resolve(newscope,"$",false)),"resolving new stack");
        CLL_MERGE(&newstack->ltvs,&oldstack->ltvs,HEAD);
        LTV_erase(oldscope,oldstack); // delete the old stack's handle ("$")
        STRY(!stack_put(thread,oldscope),"pushing scope onto stack");
    done:
        return status;
    }

    int function_close() {
        return eval_push(thread,TOK_new(TOK_EXPR,LTV_init(NEW(LTV),">/",2,LT_NONE))) || // to close and delete lambda scope
            eval_push(thread,TOK_new(TOK_EXPR,peek(dict(thread)))); // to eval lambda
    }

    int append() {
        int status=0;
        LTV *a=NULL,*b=NULL;
        char *buf;
        STRY(!(b=stack_get(thread,POP)),"popping append b");
        STRY(!(a=stack_get(thread,POP)),"popping append a");
        STRY(!(buf=mymalloc(a->len+b->len)),"allocating merge buf");
        strncpy(buf,a->data,a->len);
        strncpy(buf+a->len,b->data,b->len);
        STRY(!stack_put(thread,LTV_init(NEW(LTV),buf,a->len+b->len,LT_OWN)),"pushing merged LTV");
        LTV_release(a);
        LTV_release(b);
    done:
        return status;
    }

    int map(int pop) {
        int status=0;
        LTV *expr_ltv=NULL,*lambda_ltv=NULL;
        TOK *map_tok=NULL;

        STRY(!(lambda_ltv=stack_get(thread,POP)),"popping lambda");
        if (ref_head) { // prep ref for iteration
            STRY(edict_resolve(thread,&ref_tok->children,false),"resolving ref for deref");
            STRY(!(map_tok=TOK_cut(ref_tok)),"cutting ref tok for map");
            if (pop)
                map_tok->flags|=TOK_POP;
        }
        else // pop/prep expression for iteration
            STRY(!(map_tok=TOK_new(TOK_EXPR,stack_get(thread,POP))),"allocating map expr");

        STRY(!(push(&map_tok->lambdas,lambda_ltv)),"pushing lambda into map tok");
        STRY(eval_push(thread,map_tok),"pushing map tok");

    done:
        return status;
    }

    int eval() { // map too!
        int status=0;
        if (ref_head) // popping iteration
            STRY(map(true),"delegating map w/pop");
        else {
            LTV *lambda=stack_get(thread,POP);
            STRY(eval_push(thread,TOK_new(TOK_EXPR,lambda)),"pushing lambda");
        }
    done:
        return status;
    }

    int compare() { // compare either TOS/NOS, or TOS/name
        int status=0;
        // FIXME
    done:
        return status;
    }

    ////////////////////////////////////////////////////////////////////////////
    // create a ref if necessary
    ////////////////////////////////////////////////////////////////////////////

    if ((ref_tok=toks_peek(&ops_tok->children))) {
        LTV *ref_ltv=tok_peek(ref_tok);
        wildcard=LTV_wildcard(ref_ltv);
        STRY((assignment && wildcard),"testing for assignment-to-wildcard");
        STRY(REF_create(ref_ltv->data,ref_ltv->len,&ref_tok->children),"creating REF"); // ref tok children are REFs, not TOKs!!!
        void *resolve_macro(CLL *cll) {
            int status=0;
            REF *ref=(REF *) cll;
            LTV *ltv=peek(&ref->keys);
            if (series(ltv->data,ltv->len,NULL,NULL,"``")==ltv->len) { // macro
                CLL ref;
                CLL_init(&ref);
                STRY(REF_create(ltv->data+1,ltv->len-2,&ref),"creating REF"); // ref tok children are LT REFs, not TOKs!
                STRY(edict_resolve(thread,&ref,false),"resolving macro");
                LTV *res_ltv=REF_ltv(REF_HEAD(&ref));
                LTV_renew(ltv,res_ltv->data,res_ltv->len,LT_DUP);
            }
        done:
            return status?NON_NULL:NULL;
        }
        STRY(CLL_map(&ref_tok->children,FWD,resolve_macro)!=NULL,"resolving ref macros");
        ref_head=REF_HEAD(&ref_tok->children);
    }

    ////////////////////////////////////////////////////////////////////////////
    // iterate over ops
    ////////////////////////////////////////////////////////////////////////////

    //edict_graph_to_file("/tmp/jj.dot",thread->edict)

    if (!thread->skip && CLL_EMPTY(exceptions(thread)) && !opslen && ref_head) // implied deref
        STRY(deref(),"performing implied deref");
    else
        for (int i=0;i<opslen;i++) {
            if (thread->skipdepth) // just keep track of nestings
                switch (ops[i]) {
                    case '<': case '(':                        thread->skipdepth++; break;
                    case '>': case ')': if (thread->skipdepth) thread->skipdepth--; break;
                    default: break;
                }
            else if (peek(exceptions(thread)))
                switch (ops[i]) {
                    case '<': case '(': thread->skipdepth++; break; // ignore intervening contexts
                    case '>': STRY(scope_close(),   "evaluating scope_close (ex)");    break;
                    case ')': STRY(function_close(),"evaluating function_close (ex)"); break;
                    case '|': STRY(catch(),         "evaluating catch (ex)");          break;
                    default: break;
                }
            else if (thread->skip)
                switch (ops[i]) {
                    case '<': case '(': thread->skipdepth++; break; // ignore intervening contexts
                    case '>': thread->skip=false; STRY(scope_close(),   "evaluating scope_close (skip)");    break;
                    case ')': thread->skip=false; STRY(function_close(),"evaluating function_close (skip)"); break;
                    default: break;
                }
            else
                switch (ops[i]) {
                    case '|': thread->skip=true; goto done; // skip over exception handlers
                    case '<': STRY(scope_open(),    "evaluating scope_open");     break;
                    case '>': STRY(scope_close(),   "evaluating scope_close");    break;
                    case '(': STRY(scope_open(),    "evaluating function_open");  break;
                    case ')': STRY(function_close(),"evaluating function_close"); break;
                    case '&': STRY(throw(NULL),     "evaluating throw");          break;
                    case '!': STRY(eval(),          "evaluating eval");           break;
                    case '%': STRY(map(false),      "evaluating map");            break;
                    case '@': STRY(assign(),        "evaluating assign");         break;
                    case '/': STRY(remove(),        "evaluating remove");         break;
                    case '+': STRY(append(),        "evaluating append");         break;
                    case '=': STRY(compare(),       "evaluating compare");        break;
                    case '#': STRY(special(),       "evaluating special");        break;
                    default:
                        printf("skipping unrecognized OP %c (%d)",ops[i],ops[i]);
                        break;
                }
        }

    //edict_graph_to_file("/tmp/jj.dot",thread->edict);
 done:
    TOK_free(ops_tok);
    return status;
}

int lit_eval(THREAD *thread,TOK *tok)
{
    int status=0;
    if (!thread->skip && CLL_EMPTY(exceptions(thread)))
        STRY(!stack_put(thread,tok_pop(tok)),"pushing expr lit");
    TOK_free(tok);
 done:
    return status;
}

int expr_eval(THREAD *thread,TOK *tok)
{
    int status=0;
    TOK *child=NULL;
    LTV *lambda=NULL;

    if ((child=toks_pop(&tok->children))) {
        if ((lambda=peek(&tok->lambdas)))
            STRY(eval_push(thread,TOK_new(TOK_EXPR,lambda)),"pushing lambda expr");
        STRY(eval_push(thread,child),"pushing child");
    } else {
        thread->skip=false; // exception handlers end at end of expressions
        TOK_free(tok);
    }

 done:
    return status;
}

int ffi_eval(THREAD *thread,TOK *tok)
{
    int status=0;
    LTV *lambda=NULL;
    STRY(!(lambda=tok_peek(tok)),"peeking in tok for ffi lambda");
    CLL args; CLL_init(&args); // list of ffi arguments
    LTV *rval=NULL;
    STRY(!(rval=ref_rval_create(lambda)),"creating ffi rval ltv");
    int marshal(char *name,LTV *type) {
        int status=0;
        LTV *arg=NULL, *coerced=NULL;
        STRY(!(arg=stack_get(thread,POP)),"popping ffi arg from stack"); // FIXME: attempt to resolve by name first
        STRY(!(coerced=ref_coerce(arg,type)),"coercing ffi arg");
        LTV_enq(&args,coerced,HEAD); // enq coerced arg onto args CLL
        LT_put(rval,name,HEAD,coerced); // coerced args are installed as childen of rval
        LTV_release(arg);
    done:
        return status;
    }
    STRY(ref_args_marshal(lambda,marshal),"marshalling ffi args"); // pre-
    STRY(ref_ffi_call(lambda,rval,&args),"calling ffi");
    stack_put(thread,rval);
    CLL_release(&args,LTVR_release);
    TOK_free(tok);
 done:
    return status;
}

int file_eval(THREAD *thread,TOK *tok)
{
    int status=0;
    char *line=NULL;
    int len=0;
    TOK *expr=NULL;
    LTV *tok_data=NULL;

    STRY(!tok,"validating file tok");
    STRY(!(tok_data=tok_peek(tok)),"validating file");
    TRYCATCH(!CLL_EMPTY(exceptions(thread)),0,close_file,"checking for file read during exception");
    TRYCATCH((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,0,close_file,"reading from file");
    TRYCATCH(!(expr=TOK_new(TOK_EXPR,LTV_init(NEW(LTV),line,len,LT_OWN))),TRY_ERR,free_line,"allocating expr tok");
    TRYCATCH(eval_push(thread,expr),TRY_ERR,free_expr,"enqueing expr token");
    goto done; // success!

 free_expr:  TOK_free(expr);
 free_line:  free(line);
 close_file:
    if (tok_data->data!=stdin)
        fclose((FILE *) tok_data->data);
    TOK_free(tok);

 done:
    return status;
}

int tok_eval(THREAD *thread,TOK *tok)
{
    int status=0;
    TRY(!tok,"testing for null tok");
    CATCH(status,0,goto done,"catching null tok");

    switch(tok->flags&(TOK_FILE | TOK_EXPR | TOK_LIT | TOK_ATOM | TOK_REF | TOK_FFI))
    {
        case TOK_FILE: STRY(file_eval(thread,tok),"evaluating file"); break;
        case TOK_LIT : STRY(lit_eval(thread,tok), "evaluating lit");  break;
        case TOK_ATOM: STRY(atom_eval(thread,tok),"evaluating atom"); break;
        case TOK_REF : STRY(ref_eval(thread,tok), "evaluating ref");  break; // used for map
        case TOK_FFI : STRY(ffi_eval(thread,tok), "evaluating ffi");  break;
        case TOK_EXPR: STRY(expr_eval(thread,tok),"evaluating expr"); break;
        default: TOK_free(tok); break;
    }

 done:
    return status;
}

int thread_eval(THREAD *thread)
{
    int status=0;
    TOK *tok=NULL;
    STRY(!thread,"testing for null thread");

    if ((tok=toks_peek(toks(thread))))
        STRY(tok_eval(thread,tok),"evaluating tok");
    else
        THREAD_free(thread);

 done:
    return status;
}



///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// edict
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

// ultimately, an edict will be simply be an LTV with certain items embedded (i.e. stack, threads)
int edict_init(EDICT *edict,char *buf)
{
    int status=0;
    STRY(!edict,"validating edict");
    CLL_init(&edict->threads);
    STRY(!(edict->root=LTV_init(NEW(LTV),"ROOT",TRY_ERR,LT_RO)),"creating edict root");
    STRY(!LTI_resolve(edict->root,"$",true),"creating global stack");

    THREAD *thread=NULL;
    STRY(!(thread=THREAD_new(edict)),"creating initial thread");
    STRY(eval_push(thread,TOK_expr(buf,-1)),"injecting buf into initial thread");

    while ((thread=(THREAD *) CLL_ROT(&edict->threads,FWD)))
        STRY(thread_eval(thread),"evaluating thread");

 done:
    return status;
}

void edict_destroy(EDICT *edict)
{
    void THREAD_release(CLL *lnk) { THREAD_free((THREAD *) lnk); }
    if (edict) {
        CLL_release(&edict->threads,THREAD_release);
        LTV_release(edict->root);
    }
}

int edict(int argc,char *argv[])
{
    int status=0;
    ref_bootstrap(argc,argv);
    EDICT *edict;
    try_reset();
    try_depth=1; // superficial info only
    STRY(!(edict=NEW(EDICT)),"allocating edict");

    switch(argc) {
        case 2:  STRY(edict_init(edict,argv[1]),"evaluating argv[1]"); break;
        default: STRY(edict_init(edict,("[bootstrap.edict] #read")),"bootstrapping edict"); break;
    }

 done:
    edict_destroy(edict);
    return status;
}
