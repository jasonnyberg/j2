
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


typedef struct EDICT
{
    LTV *root;
    CLL contexts;
} EDICT;

//////////////////////////////////////////////////

enum { EVAL_SUCCESS=0, EVAL_ITER };

enum {
    DEBUG_FILE      = 1<<0,
    DEBUG_ATOM      = 1<<1,
    DEBUG_EXPR      = 1<<2,
    DEBUG_BAIL      = 1<<3,
    DEBUG_PREEVAL   = 1<<4,
    DEBUG_POSTEVAL  = 1<<5,
    DEBUG_ERR       = 1<<6
};

int debug_dump=0;
int debug=DEBUG_BAIL|DEBUG_ERR;

//////////////////////////////////////////////////

struct TOK;
struct CONTEXT;

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

CLL tok_repo;
int tok_count=0;

typedef enum {
    TOK_NONE     =0,
    TOK_POP      =1<<0x00,
    TOK_FILE     =1<<0x01,
    TOK_EXPR     =1<<0x02,
    TOK_LIT      =1<<0x03,
    TOK_OPS      =1<<0x04,
    TOK_REF      =1<<0x05,
} TOK_FLAGS;

struct TOK;
typedef struct TOK TOK;

typedef struct TOK {
    CLL lnk;
    CLL ltvs;
    CLL lambdas;
    CLL children;
    TOK_FLAGS flags;
} TOK;

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv);
void TOK_free(TOK *tok);

void show_tok(FILE *ofile,char *pre,TOK *tok,char *post);
void show_toks(FILE *ofile,char *pre,CLL *toks,char *post);

//////////////////////////////////////////////////
// REPL Context
//////////////////////////////////////////////////

typedef struct CONTEXT {
    CLL lnk;
    EDICT *edict;
    CLL dict;        // cll of ltvr
    CLL stack;       // cll of ltvr (grows at tail)
    CLL toks;        // cll of tok
    void *exception;
    int skipdepth;
    int skip;
} CONTEXT;

CONTEXT *CONTEXT_new();
void CONTEXT_free(CONTEXT *context);

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

TOK *toks_push(CLL *cll,TOK *tok) { return (TOK *) CLL_put(cll,&tok->lnk,HEAD); }
TOK *toks_pop(CLL *cll)           { return (TOK *) CLL_get(cll,POP,HEAD); }
TOK *toks_peek(CLL *cll)          { return (TOK *) CLL_get(cll,KEEP,HEAD); }

LTV *tok_push(TOK *tok,LTV *ltv) { return LTV_enq(&tok->ltvs,ltv,HEAD); }
LTV *tok_pop(TOK *tok )          { return LTV_deq(&tok->ltvs,HEAD);     }
LTV *tok_peek(TOK *tok)          { return LTV_peek(&tok->ltvs,HEAD);    }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&tok_repo);

    TOK *tok=NULL;
    if (ltv && (tok=toks_pop(repo)) || ((tok=NEW(TOK)) && CLL_init(&tok->lnk)))
    {
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

void TOK_release(CLL *lnk) { TOK_free((TOK *) lnk); }

void TOK_free(TOK *tok)
{
    if (!tok) return;
    TOK_cut(tok);
    CLL_release(&tok->ltvs,LTVR_release);
    CLL_release(&tok->lambdas,LTVR_release);
    if (tok->flags&TOK_REF) // ref tok children are LT REFs
        REF_delete(&tok->children);
    else
        CLL_release(&tok->children,TOK_release);

    toks_push(&tok_repo,tok);
    tok_count--;
}

TOK *TOK_expr(char *buf,int len) { return TOK_new(TOK_EXPR,LTV_new(buf,len,LT_NONE)); } // ownership of buf is external

void show_tok_flags(FILE *ofile,TOK *tok)
{
    if (tok->flags&TOK_POP)     fprintf(ofile,"POP ");
    if (tok->flags&TOK_FILE)    fprintf(ofile,"FILE ");
    if (tok->flags&TOK_EXPR)    fprintf(ofile,"EXPR ");
    if (tok->flags&TOK_LIT)     fprintf(ofile,"LIT ");
    if (tok->flags&TOK_OPS)     fprintf(ofile,"OPS ");
    if (tok->flags&TOK_REF)     fprintf(ofile,"REF ");
}

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
// REPL Context
//////////////////////////////////////////////////

LTV *dict_push(CONTEXT *context,LTV *ltv)  { return (LTV *) LTV_enq(&context->dict,ltv,HEAD); }
LTV *dict_pop(CONTEXT *context)            { return (LTV *) LTV_deq(&context->dict,HEAD); }
LTV *dict_peek(CONTEXT *context)           { return (LTV *) LTV_peek(&context->dict,HEAD); }

LTV *stack_push(CONTEXT *context,LTV *ltv) { return (LTV *) LTV_enq(&context->stack,ltv,HEAD); }
LTV *stack_pop(CONTEXT *context)           { return (LTV *) LTV_deq(&context->stack,HEAD); }
LTV *stack_peek(CONTEXT *context)          { return (LTV *) LTV_peek(&context->stack,HEAD); }

CONTEXT *CONTEXT_new(EDICT *edict)
{
    int status=0;
    CONTEXT *context=NULL;

    STRY(!(context=NEW(CONTEXT)),"allocating context");
    TRYCATCH(!(context->edict=edict),-1,release_context,"assigning context's edict");
    CLL_init(&context->dict);
    CLL_init(&context->stack);
    CLL_init(&context->toks);
    context->exception=NULL;
    context->skipdepth=0;
    context->skip=false;

    TRYCATCH(!dict_push(context,edict->root),-1,release_context,"pushing context->dict root");
    TRYCATCH(!CLL_put(&edict->contexts,&context->lnk,HEAD),-1,release_context,"pushing context into edict");

    goto done; // success!

 release_context:
    CONTEXT_free(context);
    context=NULL;

 done:
    return context;
}

void CONTEXT_free(CONTEXT *context)
{
    if (!context) return;
    CLL_cut(&context->lnk); // remove from any list it's in
    void tok_free(CLL *lnk) { TOK_free((TOK *) lnk); }
    CLL_release(&context->stack,LTVR_release);
    CLL_release(&context->toks,tok_free);
    RELEASE(context);
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
            fprintf(ofile,"\"%x\" [shape=box label=\"",tok);
            show_tok_flags(ofile,tok);
            fprintf(ofile,"\"]\n\"%x\" -> \"%x\" [color=red]\n",tok,lnk->lnk[0]);
            fprintf(ofile,"\"%2$x\" [label=\"ltvs\"]\n\"%1$x\" -> \"%2$x\"\n",tok,&tok->ltvs);
            ltvs2dot(ofile,&tok->ltvs,0,NULL);
            fprintf(ofile,"\"%2$x\" [label=\"lambdas\"]\n\"%1$x\" -> \"%2$x\"\n",tok,&tok->lambdas);
            ltvs2dot(ofile,&tok->lambdas,0,NULL);
            if (CLL_HEAD(&tok->children)) {
                fprintf(ofile,"\"%x\" -> \"%x\"\n",tok,&tok->children);
                if (tok->flags&TOK_REF)
                    REF_dot(ofile,&tok->children,"Refs");
                else
                    descend_toks(&tok->children,"Subtoks");
            }
        }

        fprintf(ofile,"\"%x\" [label=\"%s\"]\n",toks,label);
        fprintf(ofile,"\"%x\" -> \"%x\" [color=red]\n",toks,toks->lnk[0]);
        CLL_map(toks,FWD,op);
    }

    void show_context(CONTEXT *context) {
        int halt=0;
        fprintf(ofile,"\"Context %x\"\n",context);
        fprintf(ofile,"\"S%2$x\" [label=\"Stack\"]\n\"Context %1$x\" -> \"S%2$x\" -> \"%2$x\"\n",context,&context->stack);
        ltvs2dot(ofile,&context->stack,0,NULL);
        fprintf(ofile,"\"D%2$x\" [label=\"Dict\"]\n\"Context %1$x\" -> \"D%2$x\" -> \"%2$x\"\n",context,&context->dict);
        ltvs2dot(ofile,&context->dict,0,NULL);
        fprintf(ofile,"\"Context %x\" -> \"%x\"\n",context,&context->toks);
        descend_toks(&context->toks,"Toks");
    }

    void show_contexts(char *pre,CLL *contexts,char *post) {
        void *op(CLL *lnk) { show_context((CONTEXT *) lnk); return NULL; }
        if (pre) fprintf(ofile,"%s",pre);
        CLL_map(contexts,FWD,op);
        if (post) fprintf(ofile,"%s",post);
    }

    if (!edict) goto done;

    fprintf(ofile,"digraph iftree\n{\ngraph [/*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");

    fprintf(ofile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(ofile,"ltv_count [label=\"ltv_count %d\"]\n",ltv_count);
    fprintf(ofile,"ltvr_count [label=\"ltvr_count %d\"]\n",ltvr_count);
    fprintf(ofile,"lti_count [label=\"lti_count %d\"]\n",lti_count);

    show_contexts("",&edict->contexts,"\n");

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
// parser
//////////////////////////////////////////////////

#define EDICT_OPS "|&!%#@/+="
#define EDICT_MONO_OPS "()<>{}"

int parse_expr(TOK *tok)
{
    int status=0;
    char *data=NULL;
    int len=0,tlen=0;
    LTV *tokval=NULL;

    int advance(int bump) { bump=MIN(bump,len); data+=bump; len-=bump; return bump; }

    TOK *append(TOK *tok,int type,char *data,int len,int adv) {
        TOK *subtok=TOK_new(type,LTV_new(data,len,type==TOK_LIT?LT_DUP:LT_NONE)); // only LITs need to be duped
        if (!subtok) return NULL;
        advance(adv);
        return (TOK *) CLL_put(&tok->children,&subtok->lnk,TAIL);
    }

    STRY(!tok,"testing for null tok");
    STRY(!(tokval=tok_peek(tok)),"testing for tok ltvr value");
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
            STRY(!append(tok,TOK_OPS,data,1,1),"appending %c",*data);
        else { // ANYTHING else is ops and/or ref
            TOK *ops=NULL;
            tlen=series(data,len,EDICT_OPS,NULL,NULL); // ops
            STRY(!(ops=append(tok,TOK_OPS,data,tlen,tlen)),"appending ops");
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

int eval_push(CONTEXT *context,TOK *tok) { // engine pops
    int status=0;
    if (tok && tok->flags&TOK_EXPR && CLL_EMPTY(&tok->children))
        STRY(parse_expr(tok),"parsing expr");
    STRY(!toks_push(&context->toks,tok),"pushing tok to eval stack");
 done:
    return status;
}

int edict_resolve(CONTEXT *context,CLL *ref,int insert) { // may need to insert after a failed resolve!
    int status=0;
    STRY(!context || !ref,"validating args");
    void *dict_resolve(CLL *lnk) { return REF_resolve(((LTVR *) lnk)->ltv,ref,insert)?NULL:NON_NULL; } // if lookup failed, continue map by returning NULL
    status=!CLL_map(&context->dict,FWD,dict_resolve);
 done:
    return status;
}

int ref_eval(CONTEXT *context,TOK *ref_tok)
{
    int status=0;
    REF *ref_head=NULL;
    LTV *lambda_ltv=NULL,*ref_ltv=NULL;

    STRY(!(lambda_ltv=LTV_peek(&ref_tok->lambdas,HEAD)),"validating ref lambda");
    STRY(!(ref_head=REF_HEAD(&ref_tok->children)),"validating ref head");
    STRY(!(ref_ltv=REF_ltv(ref_head)),"validating deref result");
    STRY(!stack_push(context,ref_ltv),"pushing resolved ref to stack");
    STRY(eval_push(context,TOK_new(TOK_EXPR,lambda_ltv)),"pushing lambda expr");
    TRYCATCH(REF_iterate(&ref_tok->children,ref_tok->flags&TOK_POP),0,terminate,"iterating ref");
    TRYCATCH(!REF_ltv(ref_head),0,terminate,"iterating ref");

    goto done; // success!
 terminate:
    TOK_free(ref_tok);
 done:
    return status;
}

int ops_eval(CONTEXT *context,TOK *ops_tok) // ops contains refs in children
{
    int status=0;
    LTV *ltv=NULL; // optok's data

    STRY(!(ltv=tok_peek(ops_tok)),"getting optok data");
    char *ops=(char *) ltv->data;
    int opslen=ltv->len;

    int assignment=series(ops,opslen,NULL,"@",NULL)<opslen; // ops contains "@", i.e. assignment
    int wildcard=0;

    TOK *ref_tok=toks_peek(&ops_tok->children);
    REF *ref_head=NULL;

    if (ref_tok) {
        LTV *ref_ltv=tok_peek(ref_tok);
        wildcard=LTV_wildcard(ref_ltv);
        STRY((assignment && wildcard),"testing for assignment-to-wildcard");
        STRY(REF_create(ref_ltv->data,ref_ltv->len,&ref_tok->children),"creating REF"); // ref tok children are REFs!!!
        void *resolve_macro(CLL *cll) {
            int status=0;
            REF *ref=(REF *) cll;
            LTV *ltv=LTV_peek(&ref->keys,HEAD);
            if (series(ltv->data,ltv->len,NULL,NULL,"``")==ltv->len) { // macro
                CLL ref;
                CLL_init(&ref);
                STRY(REF_create(ltv->data+1,ltv->len-2,&ref),"creating REF"); // ref tok children are LT REFs, not TOKs!
                STRY(edict_resolve(context,&ref,false),"resolving macro");
                LTV *res_ltv=REF_ltv(REF_HEAD(&ref));
                LTV_renew(ltv,res_ltv->data,res_ltv->len,LT_DUP);
            }
        done:
            return status?NON_NULL:NULL;
        }
        STRY(CLL_map(&ref_tok->children,FWD,resolve_macro)!=NULL,"resolving ref macros");
        ref_head=REF_HEAD(&ref_tok->children);
    }

    int throw(void *exception) {
        context->exception=exception;
        return 0;
    }

    int special() { // handle special operations
        int readfrom()  {
            int status=0;
            LTV *ltv_ifilename=NULL;
            STRY(!(ltv_ifilename=stack_pop(context)),"popping import filename");
            char *ifilename=bufdup(ltv_ifilename->data,ltv_ifilename->len);
            FILE *ifile=strncmp("stdin",ifilename,5)?fopen(ifilename,"r"):stdin;
            if (ifile) {
                TOK *file_tok=TOK_new(TOK_FILE,LTV_new((void *) ifile,sizeof(FILE *),LT_IMM));
                STRY(eval_push(context,file_tok),"pushing file");
            }
            myfree(ifilename,strlen(ifilename)+1);
            LTV_release(ltv_ifilename);
        done:
            return status;
        }

        int import() {
            int status=0;
            LTV *mod_ltv=NULL;
            STRY(!(mod_ltv=stack_peek(context)),"peeking dwarf import filename");
            STRY(ref_curate_module(mod_ltv,NULL),"importing module");
        done:
            return status;
        }

        int preview() {
            int status=0;
            LTV *mod_ltv=NULL;
            STRY(!(mod_ltv=stack_peek(context)),"peeking dwarf import filename");
            STRY(ref_preview_module(mod_ltv),"listing module");
        done:
            return status;
        }

        int cvar() {
            int status=0;
            LTV *type,*cvar;
            STRY(!(type=stack_pop(context)),"popping type");
            STRY(!(cvar=ref_create_cvar(type,NULL,NULL)),"creating cvar");
            STRY(!stack_push(context,cvar),"pushing cvar");
        done:
            return status;
        }

        int dump(char *label) {
            int status=0;
            edict_resolve(context,&ref_tok->children,false);
            LTV *cvar=NULL;
            LTI *lti=NULL;
            if ((lti=REF_lti(ref_head))) {
                CLL *ltvs=&lti->ltvs;
                graph_ltvs_to_file("/tmp/jj.dot",ltvs,0,label);
                print_ltvs(stdout,CODE_BLUE,ltvs,CODE_RESET "\n",2);
            }
            else if ((cvar=REF_ltv(ref_head)) && cvar->flags&LT_CVAR)
                ref_print_cvar(stdout,cvar);
        done:
            return status;
        }

        int error() {
            return 1;
        }

        int status=0;
        if (ref_head) {
            LTV *key=REF_key(ref_head);
            char *buf=NULL;
            if (key) {
                if      (!strnncmp(key->data,key->len,"read",-1))    STRY(readfrom(),"starting input stream");
                else if (!strnncmp(key->data,key->len,"preview",-1)) STRY(preview(),"importing module preview");
                else if (!strnncmp(key->data,key->len,"import",-1))  STRY(import(),"importing dwarf module");
                else if (!strnncmp(key->data,key->len,"new",-1))     STRY(cvar(),"creating cvar");
                else if (!strnncmp(key->data,key->len,"error",-1))   STRY(error(),"evaluating \"#error\"");
                else if (!strnncmp(key->data,key->len,"throw",-1))   STRY(throw(NON_NULL),"evaluating \"#throw\"");
                else STRY(dump(PRINTA(buf,key->len,(char *) key->data)),"dumping named item");
            }
        } else {
            //edict_graph_to_file("/tmp/jj.dot",context->edict);
            print_ltvs(stdout,CODE_BLUE,&context->stack,CODE_RESET "\n",0);
        }
    done:
        return status;
    }

    int deref() {
        int status=0;
        if (edict_resolve(context,&ref_tok->children,false) || !REF_ltv(ref_head)) // if lookup failed, push copy to stack
            STRY(!stack_push(context,LTV_dup(tok_peek(ref_tok))),"pushing unresolved ref back to stack");
	else
            STRY(!stack_push(context,REF_ltv(ref_head)),"pushing resolved ref to stack");
    done:
        return status;
    }

    int assign() { // resolve refs needs to not worry about last ltv, just the lti is important.
        int status=0;
        LTV *tos=NULL;
        TRYCATCH(edict_resolve(context,&ref_tok->children,true),0,exception,"resolving ref for assign");
        TRYCATCH(!(tos=stack_peek(context)),0,exception,"peeking anon");
        TRYCATCH(REF_assign(ref_head,tos),0,exception,"assigning anon to ref");
        stack_pop(context); // succeeded, detach anon from stack
        goto done;

    exception:
        TOK_free(ref_tok);
        context->exception=NON_NULL;
    done:
        return status;
    }

    int remove() {
        int status=0;
        if (ref_head) {
	    TRYCATCH(edict_resolve(context,&ref_tok->children,false),0,done,"resolving ref for remove");
            STRY(REF_remove(ref_head),"performing ref remove");
        } else {
            LTV_release(stack_pop(context));
        }
    done:
        return status;
    }

    int stack_open()     { return !dict_push(context,stack_pop(context)); }
    int scope_close()    { return !stack_push(context,dict_pop(context)); }
    int function_close() { return eval_push(context,TOK_new(TOK_EXPR,dict_peek(context))); }

    int append() {
        int status=0;
        LTV *a=NULL,*b=NULL;
        char *buf;
        STRY(!(b=stack_pop(context)),"popping append b");
        STRY(!(a=stack_pop(context)),"popping append a");
        STRY(!(buf=mymalloc(a->len+b->len)),"allocating merge buf");
        strncpy(buf,a->data,a->len);
        strncpy(buf+a->len,b->data,b->len);
        STRY(!stack_push(context,LTV_new(buf,a->len+b->len,LT_OWN)),"pushing merged LTV");
    done:
        return status;
    }

    int map(int pop) {
        int status=0;
        LTV *expr_ltv=NULL,*lambda_ltv=NULL;
        TOK *map_tok=NULL;

        if (ref_head) { // prep ref for iteration
	    STRY(edict_resolve(context,&ref_tok->children,false),"resolving ref for deref");
            STRY(!(map_tok=TOK_cut(ref_tok)),"cutting ref tok for map");
            if (pop)
                map_tok->flags|=TOK_POP;
            STRY(!(lambda_ltv=stack_pop(context)),"popping lambda");
        }
        else { // pop/prep expression for iteration
            STRY(!(lambda_ltv=stack_pop(context)),"popping lambda");
            STRY(!(map_tok=TOK_new(TOK_EXPR,stack_pop(context))),"allocating map expr");
        }
        STRY(!(LTV_enq(&map_tok->lambdas,lambda_ltv,HEAD)),"pushing lambda into map tok");
        STRY(eval_push(context,map_tok),"pushing map tok");

    done:
        return status;
    }

    int eval() { // map too!
        int status=0;
        if (ref_head) // popping iteration
            STRY(map(true),"delegating map w/pop");
        else
            STRY(eval_push(context,TOK_new(TOK_EXPR,stack_pop(context))),"pushing lambda");
    done:
        return status;
    }

    int catch() {
        int status=0;
        // if (context->exception == whatever) could do some pattern-matching here
        context->exception=NULL;
        return status;
    }

    int compare() { // compare either TOS/NOS, or TOS/name
        int status=0;
        // FIXME
    done:
        return status;
    }

    ////////////////////////////////////////////////////////////////////////////
    // iterate over ops
    ////////////////////////////////////////////////////////////////////////////

    //edict_graph_to_file("/tmp/jj.dot",context->edict);

    if (!context->skip && !context->exception && !opslen && ref_head) // implied deref
        STRY(deref(),"performing implied deref");
    else
        for (int i=0;i<opslen;i++) {
            if (context->skipdepth) // just keep track of nestings
                switch (ops[i]) {
                    case '<': case '(':                         context->skipdepth++; break;
                    case '>': case ')': if (context->skipdepth) context->skipdepth--; break;
                    default: break;
                }
            else if (context->exception)
                switch (ops[i]) {
                    case '<': case '(': context->skipdepth++; break;
                    case '>': STRY(scope_close(),   "evaluating scope_close (ex)");    break;
                    case ')': STRY(function_close(),"evaluating function_close (ex)"); break;
                    case '|': STRY(catch(),         "evaluating catch (ex)");          break;
                    default: break;
                }
            else if (context->skip) // exception handlers end at end of blocks
                switch (ops[i]) {
                    case '<': case '(': context->skipdepth++; break;
                    case '>': context->skip=false; STRY(scope_close(),   "evaluating scope_close (skip)");    break;
                    case ')': context->skip=false; STRY(function_close(),"evaluating function_close (skip)"); break;
                    default: break;
                }
            else
                switch (ops[i]) {
                    case '|': context->skip=true; goto done; // skip over exception handlers
                    case '<': STRY(stack_open(),    "evaluating scope_open");     break;
                    case '>': STRY(scope_close(),   "evaluating scope_close");    break;
                    case '(': STRY(stack_open(),    "evaluating function_open");  break;
                    case ')': STRY(function_close(),"evaluating function_close"); break;
                    case '&': STRY(throw(NON_NULL), "evaluating throw");          break;
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

    //edict_graph_to_file("/tmp/jj.dot",context->edict);
 done:
    TOK_free(ops_tok);
    return status;
}

int lit_eval(CONTEXT *context,TOK *tok)
{
    int status=0;
    if (!context->skip && !context->exception)
        STRY(!stack_push(context,tok_pop(tok)),"pushing expr lit");
    TOK_free(tok);
 done:
    return status;
}

int expr_eval(CONTEXT *context,TOK *tok)
{
    int status=0;
    TOK *child=NULL;
    LTV *lambda_ltv=NULL;

    if ((child=toks_pop(&tok->children))) {
        if ((lambda_ltv=LTV_peek(&tok->lambdas,HEAD))) // iterative... for each child: push lambda, child.
            STRY(eval_push(context,TOK_new(TOK_EXPR,lambda_ltv)),"pushing lambda expr");
        STRY(eval_push(context,child),"pushing child");
    } else {
        context->skip=false; // exception handlers end at end of expressions
        TOK_free(tok);
    }

 done:
    return status;
}

int file_eval(CONTEXT *context,TOK *tok)
{
    int status=0;
    char *line;
    int len;
    TOK *expr=NULL;
    LTV *tok_data;

    STRY(!tok,"validating file tok");
    STRY(!(tok_data=tok_peek(tok)),"validating file");
    TRYCATCH((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,0,close_file,"reading from file");
    TRYCATCH(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),TRY_ERR,free_line,"allocating expr tok");
    TRYCATCH(eval_push(context,expr),TRY_ERR,free_expr,"enqueing expr token");
    goto done; // success!

 free_expr:  TOK_free(expr);
 free_line:  free(line);
 close_file:
    fclose((FILE *) tok_data->data);
    TOK_free(tok);

 done:
    return status;
}

int tok_eval(CONTEXT *context,TOK *tok)
{
    int status=0;
    TRY(!tok,"testing for null tok");
    CATCH(status,0,goto done,"catching null tok");

    switch(tok->flags&(TOK_FILE | TOK_EXPR | TOK_LIT | TOK_OPS | TOK_REF))
    {
        case TOK_FILE: STRY(file_eval(context,tok),"evaluating file"); break;
        case TOK_LIT : STRY(lit_eval(context,tok), "evaluating lit");  break;
        case TOK_OPS : STRY(ops_eval(context,tok), "evaluating ops");  break;
        case TOK_REF : STRY(ref_eval(context,tok), "evaluating ref");  break; // used for map
        case TOK_EXPR: STRY(expr_eval(context,tok),"evaluating expr"); break;
        default: TOK_free(tok); break;
    }

 done:
    return status;
}

int context_eval(CONTEXT *context)
{
    int status=0;
    STRY(!context,"testing for null context");

    TOK *tok=toks_peek(&context->toks);
    if (tok)
        STRY(tok_eval(context,tok),"evaluating tok");
    else
        CONTEXT_free(context);

 done:
    return status;
}



///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// edict
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

int edict_init(EDICT *edict)
{
    int status=0;
    STRY(!edict,"validating edict");
    CLL_init(&edict->contexts);
    STRY(!(edict->root=LTV_new("ROOT",TRY_ERR,LT_NONE)),"creating edict root");
 done:
    return status;
}

int edict_eval(EDICT *edict,char *buf)
{
    int status=0;
    CONTEXT *context=NULL;
    STRY(!(context=CONTEXT_new(edict)),"creating initial context");
    STRY(eval_push(context,TOK_expr(buf,-1)),"injecting buf into initial context");

    while ((context=(CONTEXT *) CLL_ROT(&edict->contexts,FWD)))
        STRY(context_eval(context),"evaluating context");

 done:
    return status;
}

void edict_destroy(EDICT *edict)
{
    void CONTEXT_release(CLL *lnk) { CONTEXT_free((CONTEXT *) lnk); }
    if (edict) {
        CLL_release(&edict->contexts,CONTEXT_release);
        LTV_release(edict->root);
    }
}

int edict(int argc,char *argv[])
{
    int status=0;
    EDICT *edict;
    try_reset();
    STRY(!(edict=NEW(EDICT)),"allocating edict");
    STRY(edict_init(edict),"initializing edict");
    
    switch(argc) {
        case 2:  STRY(edict_eval(edict,argv[1]),"evaluating argv[1]"); break;
        default: STRY(edict_eval(edict,("[bootstrap.edict] #read")),"bootstrapping edict"); break;
    }

 done:
    edict_destroy(edict);
    return status;
}

