
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

struct TOK;
struct CONTEXT;

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
} TOK_FLAGS;

typedef struct TOK {
    CLL lnk;
    CLL ltvs;
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
    CLL dict;  // cll of ltvr
    CLL stack; // cll of ltvr (grows at tail)
    CLL toks;  // cll of tok
} CONTEXT;

CONTEXT *CONTEXT_new();
TOK *CONTEXT_inject(CONTEXT *context,TOK *tok);
void CONTEXT_free(CONTEXT *context);


//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

TOK *tokpush(CLL *cll,TOK *tok) { return (TOK *) CLL_put(cll,&tok->lnk,HEAD); }
TOK *tokpop(CLL *cll)           { return (TOK *) CLL_get(cll,POP,HEAD); }
TOK *tokpeek(CLL *cll)          { return (TOK *) CLL_get(cll,KEEP,HEAD); }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&tok_repo);

    TOK *tok=NULL;
    if (ltv && (tok=tokpop(repo)) || ((tok=NEW(TOK)) && CLL_init(&tok->lnk)))
    {
        CLL_init(&tok->ltvs);
        CLL_init(&tok->children);
        tok->flags=flags;
        LTV_enq(&tok->ltvs,ltv,HEAD);
        tok_count++;
    }
    return tok;
}

void TOK_free(TOK *tok)
{
    if (!tok) return;
    void release(CLL *lnk) { TOK_free((TOK *) lnk); }
    CLL_cut(&tok->lnk); // take it out of any list it's in
    CLL_release(&tok->ltvs,LTVR_release);
    if (tok->flags&TOK_REF) // ref tok children are LT REFs
        REF_delete(&tok->children);
    else
        CLL_release(&tok->children,release);

    tokpush(&tok_repo,tok);
    tok_count--;
}

TOK *TOK_expr(char *buf,int len) { return TOK_new(TOK_EXPR,LTV_new(buf,len,LT_NONE)); } // ownership of buf is external

void show_tok_flags(FILE *ofile,TOK *tok)
{
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
    if (tok->flags&TOK_REF)
        REF_dump(ofile,&tok->children);
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
    CLL_init(&context->stack);
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
    CLL_release(&context->stack,LTVR_release);
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
            if (CLL_HEAD(&tok->children)) {
                fprintf(ofile,"\"%x\" -> \"%x\"\n",tok,&tok->children);
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
        fprintf(ofile,"\"A%2$x\" [label=\"Stack\"]\n\"Context %1$x\" -> \"A%2$x\" -> \"%2$x\"\n",context,&context->stack);
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

#define OPS "#$@/!&|="
#define CTX "<>{}()"

int parse_expr(TOK *tok)
{
    int status=0;
    char *data=NULL,*tdata=NULL;
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
    STRY(!(tokval=LTV_peek(&tok->ltvs,HEAD)),"testing for tok ltvr value");
    STRY(tokval->flags&LT_NSTR,"testing for non-string tok ltvr value");
    STRY(!tokval->data,"testing for null tok ltvr data");

    data=tokval->data;
    len=tokval->len;

    while (len) {
        if (tlen=series(data,len,WHITESPACE,NULL,NULL)) // whitespace
            advance(tlen); // TODO: A) embed WS tokens, B) stash tail WS token at start of ops (discard intermediates), C) reinsert WS when ops done
        else if (tlen=series(data,len,NULL,NULL,"[]")) // lit
            STRY(!append(tok,TOK_LIT, data+1,tlen-2,tlen),"appending lit");
        else if (tlen=series(data,len,CTX,NULL,NULL)) // special, non-ganging op
            STRY(!append(tok,TOK_OPS,data,1,1),"appending %c",*data);
        else { // ANYTHING else is ops and/or ref
            TOK *ops=NULL;
            tlen=series(data,len,OPS ".",NULL,NULL); // ops
            STRY(!(ops=append(tok,TOK_OPS,data,tlen,tlen)),"appending ops");
            tlen=series(data,len,NULL,WHITESPACE OPS CTX,"[]"); // ref
            STRY(!append(ops,TOK_REF,data,tlen,tlen),"appending ref");
        }
    }

    done:
    return status;
}

//////////////////////////////////////////////////
// eval engine
//////////////////////////////////////////////////

int tok_eval(CONTEXT *context,TOK *tok);


int ops_eval(CONTEXT *context,TOK *ops_tok) // ops contains refs in children
{
    int status=0;
    int rerun=0; // discard ops by default

    LTV *ltv=NULL; // optok's data

    STRY(!(ltv=LTV_peek(&ops_tok->ltvs,HEAD)),"getting optok data");
    char *ops=(char *) ltv->data;
    int opslen=ltv->len;

    int relative=series(ops,opslen,NULL,".",NULL)<opslen; // ops contains ".", i.e. relative deref
    int assignment=series(ops,opslen,NULL,"@",NULL)<opslen; // ops contains "@", i.e. assignment
    int wildcard=0;

    TOK *ref_tok=NULL;
    REF *ref_head=NULL;
    REF *ref_tail=NULL;

    int resolve(int insert) { // may need to insert after a failed resolve!
        int status=0;
        STRY(!ref_tok || !ref_head,"validating ref tok");
        if (relative)
            STRY(REF_resolve(&ref_tok->children,LTV_peek(&context->stack,TAIL),insert),"performing relative resolve");
        else {
            void *dict_resolve(CLL *lnk) { return !REF_resolve(&ref_tok->children,((LTVR *) lnk)->ltv,insert)?lnk:NULL; }
            STRY(!CLL_map(&context->dict,FWD,dict_resolve),"performing dict resolve");
        }
        done:
        return status;
    }

    int special() { // handle special operations
        int readfrom()  {
            int status=0;
            LTV *ltv_ifilename=NULL;
            STRY(!(ltv_ifilename=LTV_deq(&context->stack,TAIL)),"popping import filename");
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
            STRY(!(ltv_ifilename=LTV_deq(&context->stack,TAIL)),"popping dwarf import filename");
            STRY(!(ltv_ofilename=LTV_peek(&context->stack,TAIL)),"peeking edict export filename");
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
            STRY(!(type=LTV_deq(&context->stack,TAIL)),"popping type");
            int size = 100; // TODO: figure this out
            STRY(!(cvar=LTV_new((void *) mymalloc(size),size,LT_CVAR | LT_OWN | LT_BIN | LT_LIST)),"allocating cvar ltv"); // very special node!
            STRY(!LTV_enq(&(cvar->sub.ltvs),type,HEAD),"pushing type into cvar");
            STRY(!LTV_enq(&context->stack,cvar,TAIL),"pushing cvar");
            done:
            return status;
        }

        int dump(char *label) {
            int status=0;
            resolve(0);
            STRY(!(ref_head && ref_head->lti),"validating ref_head, ref_head->lti");
            //CLL *ltvs=(ref_head->ref->flags&REF_MATCH)?&ref_head->ltvs:&ref_head->lti->ltvs;
            CLL *ltvs=&ref_head->lti->ltvs;
            graph_ltvs_to_file("/tmp/jj.dot",ltvs,0,label);
            print_ltvs(stdout,CODE_BLUE,ltvs,CODE_RESET "\n",2);
            done:
            return status;
        }

        int status=0;
        if (ref_head) {
            LTV *key=NULL;
            if ((key=LTV_peek(&ref_head->keys,HEAD))) {
                if      (!strnncmp(key->data,key->len,"read",-1))   STRY(readfrom(),"starting input stream");
                else if (!strnncmp(key->data,key->len,"d2e",-1))    STRY(d2e(),"converting dwarf to edict");
                else if (!strnncmp(key->data,key->len,"cvar",-1))   STRY(cvar(),"creating cvar");
                else STRY(dump((char *) key->data),"dumping named item");
            }
        } else {
            edict_graph_to_file("/tmp/jj.dot",context->edict);
            print_ltvs(stdout,CODE_BLUE,&context->stack,CODE_RESET "\n",0);
        }
        done:
        return status;
    }

    int deref() {
        int status=0;
        STRY(resolve(0),"resolving ref for deref");
        if (ref_head->ltvr)
            STRY(!LTV_enq(&context->stack,ref_head->ltvr->ltv,TAIL),"pushing resolved ref to stack");
        else
            STRY(!LTV_enq(&context->stack,LTV_dup(LTV_peek(&ref_tok->ltvs,HEAD)),TAIL),"pushing unresolved ref to stack");
        done:
        return status;
    }

    int assign() { // resolve refs needs to not worry about last ltv, just the lti is important.
        int status=0;
        LTV *tos=NULL;
        STRY(resolve(1),"resolving ref for assign");
        STRY(!(tos=LTV_peek(&context->stack,TAIL)),"peeking anon");
        STRY(REF_assign(&ref_head,tos),"assigning anon to ref");
        LTV_deq(&context->stack,TAIL); // succeeded, detach anon from stack
        done:
        return status;
    }

    int remove() {
        int status=0;
        if (ref_head) {
            STRY(resolve(0),"resolving ref for deref");
            TRYCATCH(!(ref_head->ltvr),0,done,"getting ref_tail ref ltvr");
            LTVR_release(&ref_head->ltvr->lnk);
        } else {
            LTV_release(LTV_deq(&context->stack,TAIL));
        }
        done:
        return status;
    }

    int stack_open() { // push anon onto the scope stack
        int status=0;
        STRY(!LTV_enq(&context->dict,LTV_deq(&context->stack,TAIL),HEAD),"pushing anon scope");
        done:
        return status;
    }

    int scope_close() { // pop scope and push back onto stack
        int status=0;
        STRY(!LTV_enq(&context->stack,LTV_deq(&context->dict,HEAD),TAIL),"popping anon scope");
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
        STRY(!(ltv=LTV_peek(&context->stack,TAIL)),"peeking at TOS");
        TRYCATCH(0!=(ltv->flags&LT_NIL),CONDITIONAL_BAIL,done,"testing for nil");
        done:
        return status;
    }

    int or() {
        int status=0;
        LTV *ltv=NULL;
        STRY(!(ltv=LTV_peek(&context->stack,TAIL)),"peeking anon");
        TRYCATCH(0==(ltv->flags&LT_NIL),CONDITIONAL_BAIL,done,"testing for non-nil");
        done:
        return status;
    }

    int eval() { // limit wildcard dereferences to exec-with-name!!!
        int status=0;
        LTV *lambda_ltv=NULL;

        STRY(!(lambda_ltv=LTV_deq(&context->stack,TAIL)),"popping lambda"); // pop lambda

        if (!ref_head) {
            TOK *lambda_tok=TOK_new(TOK_EXPR,lambda_ltv);
            STRY(!tokpush(&context->toks,lambda_tok),"pushing lambda");
        } /*  else if (!deref(1)) { //something going on here, the last iteration fails to pop the lambda!!!!!
            rerun=true;

            TOK *lambda_tok=TOK_new(TOK_LIT,lambda_ltv);
            STRY(!tokpush(&context->toks,lambda_tok),"pushing lambda"); // enq anon for eval later...

            lambda_tok=TOK_new(TOK_EXPR,lambda_ltv);
            STRY(!tokpush(&context->toks,lambda_tok),"pushing lambda"); // to exec now
        } */
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


    ref_tok=tokpeek(&ops_tok->children);
    if (ref_tok) {
        LTV *ref_ltv=LTV_peek(&ref_tok->ltvs,HEAD);
        wildcard=LTV_wildcard(ref_ltv);
        STRY(assignment && wildcard,"testing for assignment-to-wildcard");
        STRY(REF_create(ref_ltv,&ref_tok->children),"creating REF"); // ref tok children are LT REFs, not TOKs!
        ref_head=REF_HEAD(&ref_tok->children);
        ref_tail=REF_TAIL(&ref_tok->children);
    }

    iterate:
    if (!opslen && ref_head) // implied deref
        STRY(deref(),"performing implied deref");
    else for (int i=0;i<opslen;i++) {
        switch (ops[i]) {
            case '#': STRY(special(),       "evaluating special");        break;
            case '$': STRY(deref(),         "evaluating deref");          break;
            case '@': STRY(assign(),        "evaluating assign");         break;
            case '/': STRY(remove(),        "evaluating remove");         break;
            case '.': /*STRY(deref_relative(),"evaluating deref_relative");*/ break;
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

    if (wildcard && REF_iterate(&ref_tok->children))
        goto iterate;

    if (!rerun)
        TOK_free(ops_tok);

    done:
    return status;
}

int lit_eval(CONTEXT *context,TOK *tok)
{
    int status=0;
    STRY(!LTV_enq(&context->stack,LTV_deq(&tok->ltvs,HEAD),TAIL),"pushing expr lit");
    done:
    TOK_free(tok);
    return status;
}


int expr_eval(CONTEXT *context,TOK *tok)
{
    int status=0;

    if (CLL_EMPTY(&tok->children))
        STRY(parse_expr(tok),"parsing expr");

    if (!CLL_EMPTY(&tok->children))
        STRY(tok_eval(context,(TOK *) CLL_HEAD(&tok->children)),"evaluating expr children");

    done:
    if (status) {
        if (status==CONDITIONAL_BAIL) {
            if (debug&DEBUG_BAIL)
                show_tok(stderr,"Bailing on expression: ",tok,"\n");
        } else if (debug&DEBUG_ERR) {
            show_tok(stderr,"Error evaluating expression: ",tok,"\n");
        }

        TOK *subtok;
        while ((subtok=tokpop(&tok->children))) TOK_free(subtok);
        status=0;
    }

    if (CLL_EMPTY(&tok->children))
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
        default: TOK_free(tok); break;
    }

    done:
    return status;
}

int context_eval(CONTEXT *context)
{
    int status=0;
    STRY(!context,"testing for null context");

    TOK *tok=tokpeek(&context->toks);
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
