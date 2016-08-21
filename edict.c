
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

#include "trace.h" // lttng


typedef struct EDICT
{
    LTV *root;
    CLL contexts;
} EDICT;


extern int dwarf2edict(char *import,char *export);

//////////////////////////////////////////////////

#define CONDITIONAL_BAIL 1

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

struct REF;
struct TOK;
struct CONTEXT;

//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

CLL ref_repo;
int ref_count=0;

typedef enum {
    REF_NONE  = 0,
    REF_MATCH = 1<<0
} REF_FLAGS;

typedef struct REF {
    CLL lnk;
    CLL lti_parent;
    LTI *lti;
    CLL ltvs; // hold ltv in list for refcount, or cvar's descended type
    LTVR *ltvr;
    REF_FLAGS flags;
} REF;

REF *REF_new(LTI *lti);
void REF_free(REF *ref);

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

CLL tok_repo;
int tok_count=0;

typedef enum {
    TOK_NONE     =0,
    TOK_FILE     =1<<0x00,
    TOK_EXPR     =1<<0x01,
    TOK_LIT      =1<<0x02,
    TOK_OPS      =1<<0x03,
    TOK_REF      =1<<0x04,
    TOK_REV      =1<<0x05, // reverse
    TOK_WC       =1<<0x06  // wildcard
} TOK_FLAGS;

typedef struct TOK {
    CLL lnk;
    CLL ltvs;
    REF *ref;
    CLL subtoks;
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
    CLL dict;  // cll of ltvr
    CLL anons; // cll of ltvr
    CLL toks;  // cll of tok
} CONTEXT;

CONTEXT *CONTEXT_new();
TOK *CONTEXT_inject(CONTEXT *context,TOK *tok);
void CONTEXT_free(CONTEXT *context);

//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

REF *refpush(CLL *cll,REF *ref) { return (REF *) CLL_put(cll,&ref->lnk,HEAD); }
REF *refpop(CLL *cll)           { return (REF *) CLL_get(cll,POP,HEAD); }
REF *refpeep(CLL *cll)          { return (REF *) CLL_get(cll,KEEP,HEAD); }

REF *REF_new(LTI *lti)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&ref_repo);

    REF *ref=NULL;
    if ((ref=refpop(repo)) || ((ref=NEW(REF)) && CLL_init(&ref->lnk)))
    {
        CLL_init(&ref->ltvs);
        CLL_init(&ref->lti_parent);
        ref->lti=lti;
        ref->ltvr=NULL;
        ref->flags=REF_NONE;
        ref_count++;
    }
    return ref;
}

void REF_free(REF *ref)
{
    if (!ref) return;
    CLL_cut(&ref->lnk); // take it out of any list it's in
    CLL_release(&ref->ltvs,LTVR_release);
    ref->lti=NULL;
    refpush(&ref_repo,ref);
    ref_count--;
}

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

TOK *tokpush(CLL *cll,TOK *tok) { return (TOK *) CLL_put(cll,&tok->lnk,HEAD); }
TOK *tokpop(CLL *cll)           { return (TOK *) CLL_get(cll,POP,HEAD); }
TOK *tokpeep(CLL *cll)          { return (TOK *) CLL_get(cll,KEEP,HEAD); }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&tok_repo);

    TOK *tok=NULL;
    if (ltv && (tok=tokpop(repo)) || ((tok=NEW(TOK)) && CLL_init(&tok->lnk)))
    {
        CLL_init(&tok->ltvs);
        CLL_init(&tok->subtoks);
        tok->ref=NULL;
        tok->flags=flags;
        LTV_enq(&tok->ltvs,ltv,HEAD);
        tok_count++;
    }
    return tok;
}

void *TOK_freeref(TOK *tok)
{
    REF_free(tok->ref);
    return tok->ref=NULL;
}

void TOK_freerefs(TOK *tok)
{
    if (!tok) return;
    void *op(CLL *lnk) { return TOK_freeref((TOK *) lnk); }
    CLL_map(&tok->subtoks,FWD,op); // see descend!
}

void TOK_free(TOK *tok)
{
    if (!tok) return;
    CLL_cut(&tok->lnk); // take it out of any list it's in
    TOK *subtok;
    while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
    if (tok->ref) REF_free(tok->ref);
    CLL_release(&tok->ltvs,LTVR_release);
    tokpush(&tok_repo,tok);
    tok_count--;
}

TOK *TOK_expr(char *buf,int len) { return TOK_new(TOK_EXPR,LTV_new(buf,len,LT_NONE)); } // ownership of buf is external

void show_tok_flags(FILE *ofile,TOK *tok)
{
    if (tok->flags&TOK_REV)     fprintf(ofile,"REV ");
    if (tok->flags&TOK_WC)      fprintf(ofile,"WC ");
    if (tok->flags&TOK_FILE)    fprintf(ofile,"FILE ");
    if (tok->flags&TOK_EXPR)    fprintf(ofile,"EXPR ");
    if (tok->flags&TOK_ATOM)    fprintf(ofile,"ATOM ");
    if (tok->flags&TOK_OPS)     fprintf(ofile,"OPS ");
    if (tok->flags&TOK_LIT)     fprintf(ofile,"LIT ");
    if (tok->flags&TOK_REF)     fprintf(ofile,"REF ");
    if (tok->flags==TOK_NONE)   fprintf(ofile,"_ ");
}

void show_tok(FILE *ofile,char *pre,TOK *tok,char *post) {
    if (pre) fprintf(ofile,"%s",pre);
    show_tok_flags(ofile,tok);
    print_ltvs(ofile,"",&tok->ltvs,"",1);
    show_toks(ofile,"(",&tok->subtoks,")");
    if (post) fprintf(ofile,"%s",post);
    fflush(ofile);
}

void show_toks(FILE *ofile,char *pre,CLL *toks,char *post)
{
    void *op(CLL *lnk) { show_tok(ofile,NULL,(TOK *) lnk,NULL); return NULL; }
    if (toks) {
        if (pre) fprintf(ofile,"%s",pre);
        CLL_map(toks,FWD,op);
        if (post) fprintf(ofile,"%s",post);
    }
}

//////////////////////////////////////////////////
// REPL Context
//////////////////////////////////////////////////

TOK *CONTEXT_inject(CONTEXT *context,TOK *tok)
{
    if (tok)
        tokpush(&context->toks,tok);
    return tok;
}

CONTEXT *CONTEXT_new(EDICT *edict)
{
    int status=0;
    CONTEXT *context=NULL;

    STRY(!(context=NEW(CONTEXT)),"allocating context");
    TRYCATCH(!(context->edict=edict),-1,release_context,"assigning context's edict");
    CLL_init(&context->dict);
    CLL_init(&context->anons);
    CLL_init(&context->toks);

    TRYCATCH(!LTV_enq(&context->dict,edict->root,HEAD),-1,release_context,"pushing context->dict root");
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
    CLL_release(&context->anons,LTVR_release);
    CLL_release(&context->toks,tok_free);
    DELETE(context);
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
            fprintf(ofile,"\"%x\" [label=\"\" shape=box label=\"",tok);
            show_tok_flags(ofile,tok);
            fprintf(ofile,"\"]\n");
            fprintf(ofile,"\"%x\" -> \"%x\" [color=red]\n",tok,lnk->lnk[0]);
            //fprintf(ofile,"\"%x\" -> \"%x\"\n",tok,&tok->ltvs);
            fprintf(ofile,"\"%2$x\" [label=\"ltvs\"]\n\"%1$x\" -> \"%2$x\"\n",tok,&tok->ltvs);
            ltvs2dot(ofile,&tok->ltvs,0,NULL);
            if (CLL_HEAD(&tok->subtoks)) {
                fprintf(ofile,"\"%x\" -> \"%x\"\n",tok,&tok->subtoks);
                descend_toks(&tok->subtoks,"Subtoks");
            }
        }

        fprintf(ofile,"\"%x\" [label=\"%s\"]\n",toks,label);
        fprintf(ofile,"\"%x\" -> \"%x\" [color=red]\n",toks,toks->lnk[0]);
        CLL_map(toks,FWD,op);
    }

    void show_context(CONTEXT *context) {
        int halt=0;
        fprintf(ofile,"\"Context %x\"\n",context);
        fprintf(ofile,"\"A%2$x\" [label=\"Anons\"]\n\"Context %1$x\" -> \"A%2$x\" -> \"%2$x\"\n",context,&context->anons);
        ltvs2dot(ofile,&context->anons,0,NULL);
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

#define OPS "#@/!&|="
#define CTX "<>{}()"
#define ATOM_END (WHITESPACE OPS CTX)

int parse_expr(TOK *tok)
{
    int status=0;
    char *data=NULL,*tdata=NULL;
    int len=0,tlen=0;
    LTV *tokval=NULL;

    int advance(unsigned x) { x=MIN(x,len); data+=x; len-=x; return x; }

    TOK *append(TOK *tok,int type,char *data,int len,int adv) {
        if (type==TOK_REF && series(data,len,NULL,"*?",NULL)<len)
            type|=TOK_WC; // wildcard test
        TOK *subtok=TOK_new(type,LTV_new(data,len,type==TOK_LIT?LT_DUP:LT_NONE)); // only LITs need to be duped
        if (!subtok) return NULL;
        advance(adv);
        return (TOK *) CLL_splice(&tok->subtoks,&subtok->lnk,TAIL);
    }

    STRY(!tok,"testing for null tok");
    STRY(!(tokval=LTV_peek(&tok->ltvs,HEAD)),"testing for tok ltvr value");
    STRY(tokval->flags&LT_NSTR,"testing for non-string tok ltvr value");
    STRY(!tokval->data,"testing for null tok ltvr data");

    data=tokval->data;
    len=tokval->len;

    while (len) {
        if (*data=='\\') // escape
            advance(1);
        else if (tlen=series(data,len,WHITESPACE,NULL,NULL))
            advance(tlen);
        else if (tlen=series(data,len,NULL,NULL,"[]")) // lit
            STRY(!append(tok,TOK_LIT, data+1,tlen-2,tlen),"appending lit");
        else if (tlen=series(data,len,CTX,NULL,NULL)) // special, non-ganging op, no balance!
            STRY(!append(tok,TOK_OPS,data,1,1),"appending %c",*data);
        else {
            TOK *ops=NULL;
            if (tlen=series(data,len,OPS,NULL,NULL))
                STRY(!(ops=append(tok,TOK_OPS,data,tlen,tlen)),"appending ops");
            if (tlen=series(data,len,NULL,ATOM_END,"[]"))
                STRY(!append(ops?ops:tok,TOK_REF,data,tlen,tlen)),"appending ref");
        }
    }

    done:
    return status;
}

//////////////////////////////////////////////////
// eval engine
//////////////////////////////////////////////////

int tok_eval(CONTEXT *context,TOK *tok);

void *ref_eval(CONTEXT *context,TOK *ops_tok,int insert)
{
    void *stack_resolve(CLL *lnk) {
        if (!lnk) return NULL;
        LTVR *ltvr=(LTVR *) lnk;
        return ref_get(ops_tok,insert,ltvr->ltv);
    }

    return CLL_map(&context->dict,FWD,stack_resolve);
}

int ref_eval(CONTEXT *context,TOK *ref_tok)
{

}

int ops_eval(CONTEXT *context,TOK *ops_tok) // ops contains refs in subtoks
{
    int status=0;
    int rerun=0; // discard ops by default

    LTV *ltv=NULL; // optok's data
    STRY(!(ltv=LTV_peek(&ops_tok->ltvs,HEAD)),"getting optok data");
    char *ops=(char *) ltv->data;
    int opslen=ltv->len;

    TOK *ref_head=(TOK *) CLL_HEAD(&ops_tok->subtoks);
    TOK *ref_tail=(TOK *) CLL_TAIL(&ops_tok->subtoks);

    int deref(int getnext) {
        void *ref_iterate() { return (ref_tail && ref_tail->ref)?ref_getnext(ops_tok):ref_eval(context,ops_tok,0); }

        int status=0;
        LTV *ltv=NULL;
        STRY(!ref_head,"validating ref present");
        TOK *ref_tail=(TOK *) (getnext?ref_iterate():ref_eval(context,ops_tok,0));
        if (ref_tail && ref_tail->ref && (ltv=LTV_peek(&ref_tail->ref->ltvs,HEAD)))
            STRY(!LTV_enq(&context->anons,ltv,HEAD),"pushing ltv to anons");
        else
            status=CONDITIONAL_BAIL; //  ltv=LTV_NIL;
        done:
        return status;
    }


    int readfrom()  {
        int status=0;
        LTV *ltv_ifilename=NULL;
        STRY(!(ltv_ifilename=LTV_deq(&context->anons,HEAD)),"popping import filename");
        char *ifilename=bufdup(ltv_ifilename->data,ltv_ifilename->len);
        FILE *ifile=strncmp("stdin",ifilename,5)?fopen(ifilename,"r"):stdin;
        if (ifile) {
            TOK *file_tok=TOK_new(TOK_FILE,LTV_new((void *) ifile,sizeof(FILE *),LT_IMM));
            STRY(!tokpush(&context->toks,file_tok),"pushing file");
        }
        myfree(ifilename,strlen(ifilename)+1);
        LTV_release(ltv_ifilename);
        done:
        return status;
    }

    int d2e() {
        int status=0;
        LTV *ltv_ifilename=NULL,*ltv_ofilename=NULL;
        STRY(!(ltv_ifilename=LTV_deq(&context->anons,HEAD)),"popping dwarf import filename");
        STRY(!(ltv_ofilename=LTV_peek(&context->anons,HEAD)),"peeking edict export filename");
        char *ifilename=bufdup(ltv_ifilename->data,ltv_ifilename->len);
        char *ofilename=bufdup(ltv_ofilename->data,ltv_ofilename->len);
        dwarf2edict(ifilename,ofilename);
        myfree(ifilename,strlen(ifilename)+1);
        myfree(ofilename,strlen(ofilename)+1);
        LTV_release(ltv_ifilename);
        done:
        return status;
    }

    int cvar() {
        int status=0;
        LTV *type, *cvar;
        STRY(!(type=LTV_deq(&context->anons,HEAD)),"popping type");
        int size = 100; // TODO: figure this out
        STRY(!(cvar=LTV_new((void *) mymalloc(size),size,LT_CVAR | LT_OWN | LT_BIN | LT_LIST)),"allocating cvar ltv"); // very special node!
        STRY(!LTV_enq(&(cvar->sub.ltvs),type,HEAD),"pushing type into cvar");
        STRY(!LTV_enq(&context->anons,cvar,HEAD),"pushing cvar");
        done:
        return status;
    }

    int dump(char *label) {
        int status=0;
        TOK *ref_tail=NULL;
        STRY(!(ref_tail=(TOK *) ref_eval(context,ops_tok,0)),"resolving tok for '#'");
        STRY(!(ref_tail->ref && ref_tail->ref->lti),"validating ref_tail's ref, ref->lti");
        CLL *ltvs=(ref_tail->ref->flags&REF_MATCH)?&ref_tail->ref->ltvs:&ref_tail->ref->lti->ltvs;
        graph_ltvs_to_file("/tmp/jj.dot",ltvs,0,label);
        print_ltvs(stdout,CODE_BLUE,ltvs,CODE_RESET "\n",2);
        done:
        return status;
    }


    int special() { // handle special operations
        int status=0;
        if (ref_head) {
            LTV *ltv=NULL;
            if ((ltv=LTV_peek(&ref_head->ltvs,HEAD))) {
                if      (!strnncmp(ltv->data,ltv->len,"read",-1))   STRY(readfrom(),"starting input stream");
                else if (!strnncmp(ltv->data,ltv->len,"d2e",-1))    STRY(d2e(),"converting dwarf to edict");
                else if (!strnncmp(ltv->data,ltv->len,"cvar",-1))   STRY(cvar(),"creating cvar");
                else STRY(dump((char *) ltv->data),"dumping named item");
            }
        } else {
            edict_graph_to_file("/tmp/jj.dot",context->edict);
            print_ltvs(stdout,CODE_BLUE,&context->anons,CODE_RESET "\n",0);
        }
        done:
        return status;
    }

    int deref_relative() { // deref from TOS (questionabie utilty right now)
        int status=0;
        LTV *ltv=NULL;
        STRY(!(ltv=LTV_peek(&context->anons,HEAD)),"peeking anon");
        /// ???
        done:
        return status;
    }

    int deref_local() {
        int status=0;
        STRY(!ref_head,"validating ref present");
        STRY(deref(0),"resolving dereference");
        done:
        return status;
    }

    int assign() { // resolve refs needs to not worry about last ltv, just the lti is important.
        int status=0;
        STRY(!ref_head,"validating ref present");
        LTV *ltv=NULL;
        TOK *ref_tail=NULL;
        STRY(!(ltv=LTV_deq(&context->anons,HEAD)),"popping anon");
        STRY(!(ref_tail=(TOK *) ref_eval(context,ops_tok,1)),"looking up reference for '@'");
        STRY(!(ref_tail->ref && ref_tail->ref->lti),"validating ref_tail's ref, ref->lti");
        STRY(!LTV_enq(&ref_tail->ref->lti->ltvs,ltv,ref_tail->flags&TOK_REV),"adding anon to lti");
        done:
        return status;
    }

    int remove() {
        int status=0;
        TOK *ref_tail=NULL;
        if (ref_head) {
            TRYCATCH(!(ref_tail=(TOK *)  ref_eval(context,ops_tok,0)),0,done,"looking up reference for '/'");
            TRYCATCH(!(ref_tail->ref) || !(ref_tail->ref->ltvr),0,done,"getting ref_tail ref ltvr");
            LTVR_release(&ref_tail->ref->ltvr->lnk);
            TOK_freeref(ref_tail);
        } else {
            LTV_release(LTV_deq(&context->anons,HEAD));
        }
        done:
        return status;
    }

    int stack_open() { // push anon onto the scope stack
        int status=0;
        STRY(!LTV_enq(&context->dict,LTV_deq(&context->anons,HEAD),HEAD),"pushing anon scope");
        done:
        return status;
    }

    int scope_close() { // pop scope and push back onto anons
        int status=0;
        STRY(!LTV_enq(&context->anons,LTV_deq(&context->dict,HEAD),HEAD),"popping anon scope");
        done:
        return status;
    }

    int function_close() { // pop scope and evaluate
        int status=0;
        TOK *expr=TOK_new(TOK_EXPR,LTV_deq(&context->dict,HEAD));
        STRY(!tokpush(&context->toks,expr),"pushing exec expr lambda");
        done:
        return status;
    }

    int and() {
        int status=0;
        LTV *ltv=NULL;
        STRY(!(ltv=LTV_peek(&context->anons,HEAD)),"peeking anon");
        TRYCATCH(0!=(ltv->flags&LT_NIL),CONDITIONAL_BAIL,done,"testing for nil");
        done:
        return status;
    }

    int or() {
        int status=0;
        LTV *ltv=NULL;
        STRY(!(ltv=LTV_peek(&context->anons,HEAD)),"peeking anon");
        TRYCATCH(0==(ltv->flags&LT_NIL),CONDITIONAL_BAIL,done,"testing for non-nil");
        done:
        return status;
    }

    int eval() { // limit wildcard dereferences to exec-with-name!!!
        int status=0;
        LTV *lambda_ltv=NULL;

        STRY(!(lambda_ltv=LTV_deq(&context->anons,HEAD)),"popping lambda"); // pop lambda

        if (!ref_head) {
            TOK *lambda_tok=TOK_new(TOK_EXPR,lambda_ltv);
            STRY(!tokpush(&context->toks,lambda_tok),"pushing lambda");
        }
        else if (!deref(1)) { //something going on here, the last iteration fails to pop the lambda!!!!!
            rerun=true;

            TOK *lambda_tok=TOK_new(TOK_LIT,lambda_ltv);
            STRY(!tokpush(&context->toks,lambda_tok),"pushing lambda"); // enq anon for eval later...

            lambda_tok=TOK_new(TOK_EXPR,lambda_ltv);
            STRY(!tokpush(&context->toks,lambda_tok),"pushing lambda"); // to exec now
        }
        else
            LTV_release(lambda_ltv);
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
    // iterate over ops
    ////////////////////////////////////////////////////////////////////////////

    for (int i=0;i<opslen;i++) {
        switch (ops[i]) {
            case '#': STRY(special(),       "evaluating special");        break;
            case '@': STRY(assign(),        "evaluating assign");         break;
            case '/': STRY(remove(),        "evaluating remove");         break;
            case '.': STRY(deref_relative(),"evaluating deref_relative"); break;
            case '<': TRY(stack_open(),     "evaluating stack_open (<)"); break;
            case '(': STRY(stack_open(),    "evaluating stack_open (()"); break;
            case '>': STRY(scope_close(),   "evaluating scope_close");    break;
            case ')': STRY(function_close(),"evaluating function_close"); break;
            case '&': STRY(and(),           "evaluating and");            break;
            case '|': STRY(or(),            "evaluating or");             break;
            case '=': STRY(compare(),       "evaluating compare");        break;
            case '!': STRY(eval(),          "evaluating eval");           break;
            case '{': break; // placeholder
            case '}': break; // placeholder
            default:
                printf("skipping unrecognized OP %c (%d)",ops[i],ops[i]);
                break;
        }
    }

    if (!rerun)
        TOK_free(ops_tok);

    done:
    return status;
}

int atom_eval(CONTEXT *context,TOK *atom_tok)
{
    int status=0;

    if (!CLL_HEAD(&atom_tok->subtoks))
        STRY(parse(atom_tok),"parsing");

    TOK *ops=(TOK *) CLL_HEAD(&atom_tok->subtoks);
    STRY(!(ops->flags&TOK_OPS),"testing atom TOS for ops");
    TRYCATCH(ops_eval(context,ops),status,done,"resolving op/ref"); // pass status thru

    done:
    if (!status && CLL_EMPTY(&atom_tok->subtoks))
        TOK_free(atom_tok);

    return status;
}

int val_eval(CONTEXT *context,TOK *tok)
{
    int status=0;
    STRY(!LTV_enq(&context->anons,LTV_deq(&tok->ltvs,HEAD),HEAD),"pushing expr lit");
    done:
    return status;
}


int expr_eval(CONTEXT *context,TOK *tok)
{
    int status=0;

    if (CLL_EMPTY(&tok->subtoks))
        STRY(parse(tok),"parsing expr");

    if (!CLL_EMPTY(&tok->subtoks))
        STRY(tok_eval(context,(TOK *) CLL_HEAD(&tok->subtoks)),"evaluating expr subtoks");

    done:
    if (status) {
        if (status==CONDITIONAL_BAIL) {
            if (debug&DEBUG_BAIL)
                show_tok(stderr,"Bailing on expression: [",tok,"]\n");
        } else if (debug&DEBUG_ERR) {
            show_tok(stderr,"Error evaluating expression: [",tok,"]\n");
        }

        TOK *subtok;
        while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
        status=0;
    }

    if (CLL_EMPTY(&tok->subtoks))
        TOK_free(tok); // evaluation done; Could add some kind of sentinel atom in here to let the expression repopulate itself and continue
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
    STRY(!(tok_data=LTV_peek(&tok->ltvs,HEAD)),"validating file");
    // use rlwrap if (stdin==(FILE *) tok_data->data) { printf(CODE_BLUE "j2> " CODE_RESET); fflush(stdout); }
    TRYCATCH((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,0,close_file,"reading from file");
    TRYCATCH(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),TRY_ERR,free_line,"allocating expr tok");
    TRYCATCH(!tokpush(&context->toks,expr),TRY_ERR,free_expr,"enqueing expr token");
    goto done; // success

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
    STRY(!tok,"testing for null tok");

    switch(tok->flags&(TOK_FILE | TOK_EXPR | TOK_LIT | TOK_OPS | TOK_REF))
    {
        case TOK_FILE: STRY(file_eval(context,tok),"evaluating file"); break;
        case TOK_EXPR: STRY(expr_eval(context,tok),"evaluating expr"); break;
        case TOK_LIT : STRY(lit_eval(context,tok), "evaluating lit");  break;
        case TOK_OPS : STRY(ops_eval(context,tok), "evaluating ops");  break;
        case TOK_REF : STRY(ref_eval(context,tok), "evaluating ref");  break;
        default: TOK_free(tok); break;
    }

    done:
    return status;
}

int context_eval(CONTEXT *context)
{
    int status=0;
    STRY(!context,"testing for null context");

    TOK *tok=tokpeep(&context->toks);
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
    STRY(!(edict->root=LTV_new("ROOT",TRY_ERR,0)),"creating edict root");
    done:
    return status;
}

int edict_eval(EDICT *edict,char *buf)
{
    int status=0;
    CONTEXT *context=NULL;
    STRY(!(context=CONTEXT_new(edict)),"creating initial context");
    STRY(!CONTEXT_inject(context,TOK_expr(buf,-1)),"injecting buf into initial context");

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

int edict(char *buf)
{
    int status=0;
    EDICT *edict;
    try_reset();
    STRY(!(edict=NEW(EDICT)),"allocating edict");
    STRY(edict_init(edict),"initializing edict");
    STRY(edict_eval(edict,buf),"evaluating \"%s\"",buf);
    done:
    edict_destroy(edict);
    return status;
}
