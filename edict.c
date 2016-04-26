
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

#include "cll.h"


#define _GNU_SOURCE
#define _C99
#include <stdlib.h>
#include "util.h"
#include "edict.h"

int debug_dump=0;
int prompt=1;

struct REF;
struct TOK;
struct CONTEXT;

//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

CLL ref_repo;
int ref_count=0;

typedef struct REF {
    CLL lnk;
    LTI *lti;
    CLL ltvs; // hold ltv in list for refcount
    LTVR *ltvr;
    int inserted;
} REF;

REF *REF_new(LTI *lti,int inserted);
void REF_free(REF *ref);

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

CLL tok_repo;
int tok_count=0;

typedef enum {
    TOK_NONE     =0,

    // actionables
    TOK_FILE     =1<<0x00,
    TOK_EXPR     =1<<0x01,
    TOK_ATOM     =1<<0x02,
    TOK_TYPES    =TOK_FILE | TOK_EXPR | TOK_ATOM,

    // quarks (make up atoms)
    TOK_OPS      =1<<0x03,
    TOK_VAL      =1<<0x04,
    TOK_REF      =1<<0x05,
    TOK_ELL      =1<<0x06,
    TOK_QUARKS   =TOK_OPS | TOK_VAL | TOK_REF | TOK_ELL,

    // expr modifiers
    TOK_WS       =1<<0x07,
    TOK_NOTE     =1<<0x08,
    TOK_EXEC     =1<<0x09,
    TOK_SCOPE    =1<<0x0a,
    TOK_CURLY    =1<<0x0b,
    TOK_REDUCE   =1<<0x0c,
    TOK_FLATTEN  =1<<0x0d,
    TOK_EXPRS    =TOK_WS | TOK_NOTE | TOK_EXEC | TOK_SCOPE | TOK_CURLY | TOK_REDUCE | TOK_FLATTEN,

    // repl helpers
    TOK_REV      =1<<0x10,
    TOK_CURRENT  =1<<0x11,
    TOK_WC       =1<<0x12,
    TOK_TEMP     =1<<0x13,
    TOK_REPL     =TOK_REV | TOK_CURRENT | TOK_WC | TOK_TEMP,
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

void show_tok(TOK *tok);
void show_toks(char *pre,CLL *toks,char *post);

//////////////////////////////////////////////////
// REPL Context
//////////////////////////////////////////////////

typedef struct CONTEXT {
    CLL lnk;
    CLL dict;  // cll of ltvr
    CLL anons; // cll of ltvr
    CLL toks;  // cll of tok
} CONTEXT;

CONTEXT *CONTEXT_new(TOK *tok);
void CONTEXT_free(CONTEXT *context);
void CONTEXT_release(CLL *lnk);

//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

REF *refpush(REF *ref) { return (REF *) CLL_put(&ref_repo,&ref->lnk,HEAD); }
REF *refpop()          { return (REF *) CLL_get(&ref_repo,POP,HEAD); }

REF *REF_new(LTI *lti,int inserted)
{
    REF *ref=NULL;
    if (lti && (ref=refpop()) || (ref=NEW(REF)))
    {
        CLL_init(&ref->lnk);
        CLL_init(&ref->ltvs);
        ref->lti=lti;
        ref->inserted=inserted;
        ref_count++;
    }
    return ref;
}

void REF_free(REF *ref)
{
    if (!ref) return;
    CLL_release(&ref->ltvs,LTVR_release);
    if (ref->inserted && ref->lti) LTI_release(&ref->lti->rbn);
    ref->lti=NULL;
    ref->inserted=0;
    refpush(ref);
    ref_count--;
}

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

TOK *tokpush(CLL *cll,TOK *tok) { return (TOK *) CLL_put(cll,&tok->lnk,HEAD); }
TOK *tokpop(CLL *cll)           { return (TOK *) CLL_get(cll,POP,HEAD); }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    TOK *tok=NULL;
    if (ltv && (tok=tokpop(&tok_repo)) || (tok=NEW(TOK)))
    {
        CLL_init(&tok->lnk);
        CLL_init(&tok->ltvs);
        CLL_init(&tok->subtoks);
        tok->ref=NULL;
        tok->flags=flags;
        LTV_enq(&tok->ltvs,ltv,HEAD);
        tok_count++;
    }
    return tok;
}

void TOK_free(TOK *tok)
{
    if (!tok) return;
    TOK *subtok;
    if (tok->ref) REF_free(tok->ref);
    while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
    CLL_release(&tok->ltvs,LTVR_release);
    tokpush(&tok_repo,tok);
    tok_count--;
}

void show_toks(char *pre,CLL *toks,char *post)
{
    void *op(CLL *lnk) { show_tok((TOK *) lnk); return NULL; }
    if (pre) printf("%s",pre);
    CLL_map(toks,FWD,op);
    if (post) printf("%s",post);
}

void show_tok(TOK *tok) {
    if (tok->flags&TOK_WS)      printf("WS " );
    if (tok->flags&TOK_NOTE)    printf("NOTE " );
    if (tok->flags&TOK_EXEC)    printf("EXEC " );
    if (tok->flags&TOK_SCOPE)   printf("SCOPE ");
    if (tok->flags&TOK_CURLY)   printf("CURLY ");
    if (tok->flags&TOK_REDUCE)  printf("REDUCE ");
    if (tok->flags&TOK_FLATTEN) printf("FLATTEN ");
    if (tok->flags&TOK_REV)     printf("REV ");
    if (tok->flags&TOK_FILE)    { printf("FILE \n"); print_ltvs(&tok->ltvs,1); }
    if (tok->flags&TOK_EXPR)    { printf("EXPR \n"); print_ltvs(&tok->ltvs,1); }
    if (tok->flags&TOK_ATOM)    { printf("ATOM \n"); print_ltvs(&tok->ltvs,1); }
    if (tok->flags&TOK_OPS)     { printf("OPS \n");  print_ltvs(&tok->ltvs,1); }
    if (tok->flags&TOK_VAL)     { printf("VAL \n");  print_ltvs(&tok->ltvs,1); }
    if (tok->flags&TOK_REF)     { printf("REF \n");  print_ltvs(&tok->ltvs,1); }
    if (tok->flags&TOK_ELL)     printf("ELL \n");
    if (tok->flags==TOK_NONE)   printf("_ ");
    show_toks("(",&tok->subtoks,") ");
}

//////////////////////////////////////////////////
// REPL Context
//////////////////////////////////////////////////

CONTEXT *CONTEXT_new(TOK *tok)
{
    CONTEXT *context=NULL;
    if (tok && (context=NEW(CONTEXT))) {
        CLL_init(&context->dict); CLL_init(&context->anons); CLL_init(&context->toks);
        CLL_put(&context->toks,&tok->lnk,HEAD);
    }
    return context;
}

void CONTEXT_free(CONTEXT *context)
{
    if (!context) return;
    void tok_free(CLL *lnk) { TOK_free((TOK *) lnk); }
    CLL_release(&context->anons,LTVR_release);
    CLL_release(&context->toks,tok_free);
    DELETE(context);
}

void CONTEXT_release(CLL *lnk) { CONTEXT_free((CONTEXT *) lnk); }


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

int edict_graph(EDICT *edict) {
    int status=0;
    FILE *dumpfile;

    void graph_ltvs(CLL *ltvs) {
        fprintf(dumpfile,"%d [label=\"\" shape=point color=red]\n",ltvs);
        fprintf(dumpfile,"%d -> %d [color=red]\n",ltvs,ltvs->lnk[0]);
    }

    void graph_lti(LTI *lti,int depth,int *halt) {
        fprintf(dumpfile,"\t%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn)) fprintf(dumpfile,"\t%d -> %d [color=blue]\n",rb_parent(&lti->rbn),&lti->rbn);
        fprintf(dumpfile,"%d -> %d [weight=2]\n",&lti->rbn,&lti->ltvs);
        graph_ltvs(&lti->ltvs);
    }

    void graph_ltvr(LTVR *ltvr,int depth,int *halt) {
        if (ltvr->ltv) fprintf(dumpfile,"%d -> %d [weight=2]\n",ltvr,ltvr->ltv);
        fprintf(dumpfile,"%d [label=\"\" shape=point color=brown]\n",&ltvr->lnk);
        fprintf(dumpfile,"%d -> %d [color=brown]\n",&ltvr->lnk,ltvr->lnk.lnk[0]);
    }

    void graph_ltv(LTV *ltv,int depth,int *halt) {
        if (ltv->flags&LT_AVIS && (*halt=1)) return;

        if (ltv->len && !(ltv->flags&LT_NSTR)) {
            fprintf(dumpfile,"%d [style=filled shape=box label=\"",ltv);
            fstrnprint(dumpfile,ltv->data,ltv->len);
            fprintf(dumpfile,"\"]\n");
        }
        else if (ltv->flags&LT_NIL)
            fprintf(dumpfile,"%d [label=\"NIL\" shape=box style=filled]\n",ltv);
        else
            fprintf(dumpfile,"%d [label=\"\" shape=box style=filled height=.1 width=.3]\n",ltv);

        if (ltv->sub.ltis.rb_node)
            fprintf(dumpfile,"%1$d -> %2$d [color=blue lhead=cluster_%2$d]\n\n",ltv,ltv->sub.ltis.rb_node);
    }

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        if (*lti)       graph_lti(*lti,depth,flags);
        else if (*ltvr) graph_ltvr(*ltvr,depth,flags);
        else if (*ltv)  graph_ltv(*ltv,depth,flags);
        return NULL;
    }

    void descend_ltvr(LTVR *ltvr) { listree_traverse(ltvr->ltv,preop,NULL); }
    void descend_ltvs(CLL *ltvs) {
        int halt=0;
        void *op(CLL *lnk) { graph_ltvr((LTVR *) lnk,0,&halt); descend_ltvr((LTVR *) lnk); return NULL; }
        CLL_map(ltvs,FWD,op);
    }

    void show_context(CONTEXT *context) {
        int halt=0;
        fprintf(dumpfile,"Context%d\n",context);
        fprintf(dumpfile,"A%2$d [label=\"Anons\"] Context%1$d -> A%2$d -> %2$d\n",context,&context->anons);
        graph_ltvs(&context->anons);
        descend_ltvs(&context->anons);
        fprintf(dumpfile,"D%2$d [label=\"Dict\"] Context%1$d -> D%2$d -> %2$d\n",context,&context->dict);
        graph_ltvs(&context->dict);
        descend_ltvs(&context->dict);
    }

    void show_contexts(char *pre,CLL *contexts,char *post)
    {
        void *op(CLL *lnk) { show_context((CONTEXT *) lnk); return NULL; }
        if (pre) fprintf(dumpfile,"%s",pre);
        CLL_map(contexts,FWD,op);
        if (post) fprintf(dumpfile,"%s",post);
    }

    if (!edict) goto done;

    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\ngraph [ratio=compress, concentrate=true] node [shape=record] edge []\n");

    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"ltv_count [label=\"ltv_count %d\"]\n",ltv_count);
    fprintf(dumpfile,"ltvr_count [label=\"ltvr_count %d\"]\n",ltvr_count);
    fprintf(dumpfile,"lti_count [label=\"lti_count %d\"]\n",lti_count);

    show_contexts("",&edict->contexts,"\n");

    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

done:
    return status;
}


#define OPS "$@/!&|="
#define ATOM_END (OPS WHITESPACE "<({\'\"")

int parse(TOK *tok)
{
    int status=0;
    char *data=NULL,*tdata=NULL;
    int len=0,tlen=0;
    LTV *tokval=NULL;

    int advance(unsigned x) { x=MIN(x,len); data+=x; len-=x; return x; }

    TOK *append(TOK *tok,int type,char *data,int len,int adv) {
        TOK *subtok=TOK_new(type,LTV_new(data,len,LT_DUP));
        if (!subtok) return NULL;
        advance(adv);
        return (TOK *) CLL_splice(&tok->subtoks,&subtok->lnk,TAIL);
    }

    STRY(!tok,"testing for null tok");
    TRY(!(tokval=LTV_deq(&tok->ltvs,HEAD)),0,done,"testing for tok ltvr value");

    TRY(tokval->flags&LT_NSTR,TRY_ERR,release_val,"testing for non-string tok ltvr value");
    TRY(!tokval->data,TRY_ERR,release_val,"testing for null tok ltvr data");

    data=tokval->data;
    len=tokval->len;

    if (tok->flags&TOK_EXPR) while (len) {
        switch (*data) {
            case '\\': advance(1);
            case ' ':
            case '\t':
            case '\n': tlen=series(data,len,WHITESPACE,NULL,NULL); TRY(!append(tok,TOK_EXPR|TOK_WS,     data  ,tlen  ,tlen),TRY_ERR,release_val,"appending ws");      break;
            case '#':  tlen=series(data,len,"#\n",NULL,NULL);      TRY(!append(tok,TOK_EXPR|TOK_NOTE,   data+1,tlen-2,tlen),TRY_ERR,release_val,"appending note");    break;
            case '<':  tlen=series(data,len,NULL,NULL,"<>");       TRY(!append(tok,TOK_EXPR|TOK_SCOPE,  data+1,tlen-2,tlen),TRY_ERR,release_val,"appending scope");   break;
            case '(':  tlen=series(data,len,NULL,NULL,"()");       TRY(!append(tok,TOK_EXPR|TOK_EXEC,   data+1,tlen-2,tlen),TRY_ERR,release_val,"appending exec");    break;
            case '{':  tlen=series(data,len,NULL,NULL,"{}");       TRY(!append(tok,TOK_EXPR|TOK_CURLY,  data+1,tlen-2,tlen),TRY_ERR,release_val,"appending curly");   break;
            case '\'': tlen=series(data,len,NULL,NULL,"\'\'");     TRY(!append(tok,TOK_EXPR|TOK_REDUCE, data+1,tlen-2,tlen),TRY_ERR,release_val,"appending reduce");  break;
            case '\"': tlen=series(data,len,NULL,NULL,"\"\"");     TRY(!append(tok,TOK_EXPR|TOK_FLATTEN,data+1,tlen-2,tlen),TRY_ERR,release_val,"appending flatten"); break;
            default:   tlen=series(data,len,OPS,ATOM_END,NULL);    TRY(!append(tok,TOK_ATOM,            data,  tlen,  tlen),TRY_ERR,release_val,"appending atom");    break;
        }
    }
    else if (tok->flags&TOK_ATOM) while (len) {
        TOK *ops=NULL,*ref=tok,*val=NULL;

        if (tlen=series(data,len,OPS,NULL,NULL)) // ops
            TRY(!(ops=append(tok,TOK_OPS,data,tlen,tlen)),TRY_ERR,release_val,"appending ops");

        while (len) {
            if ((tlen=series(data,len,NULL,".[",NULL))) {
                int reverse=advance(data[0]=='-')?TOK_REV:0;
                if (reverse) tlen-=1;
                TRY(!(ref=append(tok,TOK_REF|reverse,data,tlen,tlen)),TRY_ERR,release_val,"appending ref");
            }
            while ((tlen=series(data,len,NULL,NULL,"[]")))
                TRY(!(val=append(ref,TOK_VAL,data+1,tlen-2,tlen)),TRY_ERR,release_val,"appending val");
            if ((tlen=series(data,len,".",NULL,NULL))) {
                if (tlen==3)
                    TRY(!(ref=append(tok,TOK_ELL,NULL,0,0)),TRY_ERR,release_val,"appending ellipsis");
                advance(tlen);
            }
        }
    }

    release_val:
    LTV_release(tokval);

    done:
    return status;
}

#define LTV_NIL  LTV_new(NULL,0,LT_NIL)
#define LTV_NULL LTV_new(NULL,0,LT_NULL)
// FIXME: name ideas: "Harness", "Munkeywrench"

int edict_eval(EDICT *edict)
{
    int status=0;

    int eval_context(CONTEXT *context) {
        int status=0;
        TOK *tok;
        int exit_expr=0;

        int eval_tok(TOK *tok) {


            #if 0
            // FIXME: convert all these inner functions to use TRY/STRY
            int eval_atom(TOK *tok) {
                int resolve_descend(LTVR *ltvr,char *data,int len,char op) {
                    int test_exit(LTV *ltv) {
                        int nil=(!ltv || ltv->flags&LT_NIL);
                        exit_expr=(op=='&')?nil:!nil;
                    }

                    int descend_ltvr(LTVR *ltvr,char *data,int len) {
                        int descend_lti(LTI *lti,char *data,int len,int reverse) {
                            int status=0;
                            LTVR *ltvr=NULL;
                            LTVR *resolve_ltvr(char *match,int matchlen,int insert) {
                                if (!lti)
                                    return NULL;
                                if (LTV_get(&lti->ltvs,KEEP,reverse,match,matchlen,&ltvr))
                                    return ltvr;
                                if (op=='@' && insert) {
                                    LTV_put(&lti->ltvs,matchlen?LTV_new(match,matchlen,LT_DUP):LTV_NULL,reverse,&ltvr);
                                    return ltvr;
                                }
                            }

                            int process_ltvr(char *data,int len) {
                                if (!len) {
                                    LTV *ltv=NULL;
                                    switch (op) {
                                        case '@': if (!ltvr) LTV_put(&lti->ltvs,(ltv=LTV_deq(&context->anons,HEAD))?ltv:LTV_NIL,reverse,&ltvr); return 1;
                                        case '/': return (ltvr || resolve_ltvr(NULL,0,false))?LTVR_release(&ltvr->lnk),1:0;
                                        case '?': if (ltvr) print_ltv(ltvr->ltv,0); else print_ltvs(&lti->ltvs,0); return 1;
                                        case '&':
                                        case '|': return test_exit((ltvr || resolve_ltvr(NULL,0,false))?ltvr->ltv:NULL),1;
                                        case '!': return 1; // exec w/ref unhandled for now
                                        default:  return (ltvr || resolve_ltvr(NULL,0,false))?LTV_enq(&context->anons,ltvr->ltv,HEAD),1:0;
                                        break;
                                    }
                                }
                                else if (data[0]=='.')
                                    return (ltvr || resolve_ltvr(NULL,0,true)) && descend_ltvr(ltvr,data+1,len-1);
                                else return test_exit(NULL),1;
                            }

                            if (lti && len && data[0]=='[') {
                                int done=0,tlen=series(data,len,NULL,".",NULL);
                                char *ref=data+tlen;
                                int reflen=len-tlen;
                                for (; (tlen=series(data,len,NULL,NULL,"[]")); data+=tlen,len-=tlen) {
                                    char *key=data+1;
                                    int keylen=tlen-2;
                                    if (series(key,keylen,NULL,"*?",NULL)<keylen) { // wildcards
                                        int hit=0;
                                        void *LTVR_op(CLL *lnk) {
                                            LTVR *ltvr=(LTVR *) lnk;
                                            if (!fnmatch_len(key,keylen,ltvr->ltv->data,ltvr->ltv->len))
                                                process_ltvr(ref,reflen);
                                            hit|=done;
                                            return NULL;
                                        }
                                        CLL_map(&lti->ltvs,FWD,LTVR_op);
                                        done=hit;
                                    }
                                    else if (resolve_ltvr(key,keylen,true))
                                        done|=process_ltvr(ref,reflen);
                                }
                            }
                            else process_ltvr(data,len);
                        }

                        int tlen=0;
                        LTI *lti=NULL;
                        LTV *ltv=ltvr->ltv;

                        int reverse=(data[0]=='-');
                        data+=reverse;
                        len-=reverse;
                        int done=0;

                        void process_lti(LTI *lti) {
                            done=descend_lti(lti,data+tlen,len-tlen,reverse);
                            if (lti && CLL_EMPTY(&lti->ltvs))
                                RBN_release(&ltv->sub.ltis,&lti->rbn,LTI_release);
                        }

                        if ((tlen=series(data,len,NULL,".[",NULL)))
                            if (series(data,tlen,NULL,"*?",NULL)<tlen) { // wildcards
                                int hit=0;
                                void *LTI_op(RBN *rbn) {
                                    LTI *lti=(LTI *) rbn;
                                    if (!fnmatch_len(data,tlen,lti->name,strlen(lti->name)))
                                        process_lti(lti);
                                    hit|=done;
                                    return NULL;
                                }

                                LTV_map(ltv,FWD,LTI_op,NULL);
                                done=hit;
                            }
                            else process_lti(RBR_find(&ltv->sub.ltis,data,tlen,(op=='@')));   /// FIXME RESOLVE LTI
                        return done;
                    }

                    int reverse=0;
                    if (len) {
                        if (data[0]=='-' && len==1) reverse=1;
                        else return descend_ltvr(ltvr,data,len); // ops+ref
                    }
                    switch (op) { // else just ops
                        case '/': LTV_release(LTV_deq(&context->anons,reverse)); return 1;
                        case '?': print_ltvs(&context->dict,0); return 1;
                        case '&':
                        case '|': {
                            LTV *ltv=LTV_deq(&context->anons,HEAD);
                            test_exit(ltv);
                            LTV_release(ltv);
                            return 1;
                        }
                        case '!': {
                            LTV *lambda=LTV_deq(&context->anons,reverse);
                            TOK *expr=TOK_new(TOK_EXPR,lambda);
                            CLL_put(&tok->lnk,&expr->lnk,TAIL); // insert ahead of this token
                            return 1;
                        }
                        default:
                            if (reverse)
                                LTV_enq(&context->anons,LTV_deq(&context->anons,TAIL),HEAD); // rotate tail to head
                            else
                                printf("skipping noref OP %c (%d)",op,op);
                            return 1;
                    }
                }

                int resolve(char op,char *data,int len) {
                    void *descend(CLL *lnk) { return resolve_descend((LTVR *) lnk,data,len,op)?NON_NULL:NULL; }
                    int done=CLL_map(&context->dict,FWD,descend)!=NULL;
                    if (!done && !op) return LTV_enq(&context->anons,LTV_NIL,HEAD),1;
                    return done;
                }

                LTV *tok_ltv=NULL;

                STRY(!(tok_ltv=LTV_get(&tok->ltvs,KEEP,HEAD,NULL,0,NULL)),"retrieving atom tok data");

                char *data=tok_ltv->data;
                int i,len=tok_ltv->len;
                int tlen=series(data,len,OPS,NULL,NULL);

                for (i=0; (!i || i<tlen) && !exit_expr; i++)
                    resolve(tlen?data[i]:0,data+tlen,len-tlen);
#endif



            int eval_atom(TOK *atom_tok) {
                int eval(CLL *atom_subtoks) {// returns NULL only if exhausted
                    int status=0;

                    int push_anon(LTV *ltv) {
                        int status=0;
                        STRY(!LTV_enq(&context->anons,ltv,HEAD),"pushing anon");
                        done:
                        return status;
                    }

                    LTV *pop_anon() { return LTV_deq(&context->anons,HEAD); }

                    int resolve_ops(TOK *tok,char *ops,int opslen) {
                        TOK *resolve_refs(TOK *reftok,int insert) {
                            void *resolve_stackframe(CLL *lnk) {
                                int status=0;
                                TOK *tok=reftok;
                                void *descend(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
                                    TOK *next_tok=(TOK *) CLL_next(atom_subtoks,&tok->lnk,FWD);

                                    int descend_ltv() {
                                        int status=0;
                                        STRY(tok && ltv && lti,"validating tok_lti params");
                                        LTV *ref_ltv=NULL;
                                        STRY(!(ref_ltv=LTV_get(&tok->ltvs,KEEP,HEAD,NULL,0,NULL)),"getting ltvr w/name from token");
                                        int inserted=insert && !((*ltv)->flags&LT_RO) && (!(*ltvr) || !((*ltvr)->flags&LT_RO)); // directive on way in, status on way out
                                        STRY(!((*lti)=RBR_find(&(*ltv)->sub.ltis,ref_ltv->data,ref_ltv->len,&inserted)),"looking up name in ltv");
                                        REF *ref=NULL;
                                        STRY(!(ref=REF_new((*lti),inserted)),"allocating ref");
                                        tok->ref=ref;
                                        done:
                                        return status;
                                    }

                                    int descend_lti() {
                                        int status=0;
                                        STRY(!(tok && lti && ltv),"validating tok_ltv params");
                                        int reverse=tok->flags&TOK_REV;
                                        LTV *val_ltv=NULL;
                                        TOK *subtok=(TOK *) CLL_get(&tok->subtoks,KEEP,HEAD);
                                        if (subtok) {
                                            STRY(!(val_ltv=LTV_get(&subtok->ltvs,KEEP,HEAD,NULL,0,NULL)),"getting ltvr w/val from token");
                                            reverse |= subtok->flags&TOK_REV;
                                        }
                                        char *match=val_ltv?val_ltv->data:NULL;
                                        int matchlen=val_ltv?-1:0;
                                        (*ltv)=LTV_get(&(*lti)->ltvs,KEEP,reverse,match,matchlen,&(*ltvr)); // lookup

                                        // check if add is required
                                        if (!(*ltv)) {
                                            if (insert && val_ltv) // required install
                                                *ltv-val_ltv;
                                            else if (next_tok) // required install
                                                *ltv=val_ltv?val_ltv:LTV_NULL;
                                            if (*ltv) {
                                                LTV_put(&(*lti)->ltvs,*ltv,reverse,&(*ltvr));
                                                if (!insert)
                                                    (*ltvr)->flags|=LT_TEMP;
                                            }
                                        }

                                        if (*ltv)
                                            LTV_enq(&tok->ref->ltvs,(*ltv),HEAD);

                                        done:
                                        return status;
                                    }

                                    if (*ltv)
                                        STRY(descend_ltv(),"descend_ltv");
                                    else if (*lti)
                                        STRY(descend_lti(),"descend_lti");
                                    else
                                        status=-1;

                                    done:
                                    if (status || !next_tok) {
                                        *flags=LT_TRAVERSE_HALT;
                                        return tok;
                                    }

                                    tok=next_tok;
                                    return NULL; // continue descent
                                }

                                if (!lnk) return NULL;
                                TOK *rtok=listree_traverse((LTV *) lnk,descend,NULL);
                                return status?NULL:rtok;
                            }
                            return (TOK *) CLL_map(&context->dict,FWD,resolve_stackframe);
                        }

                        for (int i=0;i<opslen;i++) {
                            TOK *rtok=NULL;
                            LTI *lti=NULL;
                            LTV *ltv=NULL;
                            LTV *anon=NULL;
                            switch (ops[i]) {
                                case '$':
                                    STRY(!tok,"validating tok");
                                    STRY(!(rtok=resolve_refs(tok,0)),"looking up reference for '$'");
                                    STRY(!(rtok->ref) || !(ltv=(LTV *) CLL_get(&rtok->ref->ltvs,KEEP,HEAD)),"getting rtok ref ltv");
                                    STRY(!LTV_enq(&context->anons,ltv,HEAD),"pushing ltv to anons");
                                    break;
                                case '@': // resolve refs needs to not worry about last ltv, just the lti is important.
                                    STRY(!tok,"validating tok");
                                    STRY(!(anon=pop_anon()),"popping anon");
                                    STRY(!(rtok=resolve_refs(tok,1)),"looking up reference for '$'");
                                    STRY(!(rtok->ref) || !(rtok->ref->lti),"getting rtok ref lti");
                                    STRY(!LTV_put(&lti->ltvs,anon,rtok->flags&TOK_REV,NULL),"adding anon to lti");
                                    break;
                                case '/':
                                    if (tok) {
                                        STRY(!(rtok=resolve_refs(tok,0)),"looking up reference for '$'");
                                        STRY(!(rtok->ref) || !(rtok->ref->ltvr),"getting rtok ref ltvr");
                                        LTVR_release(&rtok->ref->ltvr->lnk);
                                    }
                                    else
                                        LTV_release(pop_anon());
                                    break;
                                case '&':
                                    break;
                                case '=': // structure copy
                                    break;
                                case '|':
                                    break;
                                case '!': // limit wildcard dereferences to exec-with-name!!!
                                    STRY(!(anon=pop_anon()),"popping anon");
                                    TOK *expr=TOK_new(TOK_EXPR,anon);
                                    CLL_put(&tok->lnk,&expr->lnk,TAIL); // insert ahead of this token
                                    break;
                                default:
                                    printf("skipping unrecognized OP %c (%d)",ops[i],ops[i]);
                                    break;
                            }
                        }

                        // FIXME: Clean up!!!

                        done:
                        return status;
                    }

                    TOK *tos=(TOK *) CLL_next(atom_subtoks,NULL,FWD);
                    if (tos->flags&TOK_OPS) {
                        LTV *ltv=NULL; // optok's data
                        STRY(!(ltv=LTV_get(&tos->ltvs,KEEP,HEAD,NULL,0,NULL)),"getting optok data");
                        STRY(resolve_ops((TOK *) CLL_next(atom_subtoks,&tos->lnk,FWD),ltv->data,ltv->len),"resolving op/ref");
                    }
                    else if (tos->flags&TOK_REF)
                        STRY(resolve_ops(tos,"$",1),"resolving implied ref");
                    else if (tos->flags&TOK_VAL)
                        STRY(push_anon(LTV_deq(&tos->ltvs,HEAD)),"pushing lit");

                    done:
                    return status;
                }

                int status=0;
                STRY(parse(atom_tok),"parsing");

                show_tok(atom_tok);

                STRY(eval(&atom_tok->subtoks),"evaluating");
                TOK_free((TOK *) CLL_cut(&atom_tok->lnk));
                done:
                return status;
            }


            int eval_expr(TOK *tok) {
                int status=0;

                if (CLL_EMPTY(&tok->subtoks)) {
                    if (parse(tok)) TOK_free((TOK *) CLL_cut(&tok->lnk)); // empty expr
                    else if (tok->flags&TOK_WS || tok->flags&TOK_NOTE)
                        TOK_free((TOK *) CLL_cut(&tok->lnk)); // for now
                    else if (tok->flags&TOK_REDUCE)
                        TOK_free((TOK *) CLL_cut(&tok->lnk)); // for now
                    else if (tok->flags&TOK_FLATTEN)
                        TOK_free((TOK *) CLL_cut(&tok->lnk)); // for now
                    else if (tok->flags&TOK_SCOPE)
                        LTV_enq(&context->dict,LTV_deq(&context->anons,HEAD),HEAD);
                    else if (tok->flags&TOK_EXEC) {
                        LTV *lambda=LTV_deq(&context->anons,HEAD);
                        TOK *expr=TOK_new(TOK_EXPR,lambda);
                        STRY(!LTV_enq(&context->dict,lambda,HEAD),"pushing lambda scope");
                        STRY(!CLL_put(&tok->subtoks,&expr->lnk,TAIL),"pushing lambda expr");
                    }
                }
                else { // evaluate
                    STRY(eval_tok((TOK *) CLL_get(&tok->subtoks,KEEP,HEAD)),"evaluating expr subtoks");
                    if (exit_expr || CLL_EMPTY(&tok->subtoks)) {
                        exit_expr=0;
                        if (tok->flags&(TOK_EXEC|TOK_SCOPE)) // nothing special for curly yet
                            LTV_release(LTV_deq(&context->dict,HEAD));
                        TOK_free((TOK *) CLL_cut(&tok->lnk)); // evaluation done
                    }
                }

                done:
                return status;
            }


            int eval_file(TOK *tok) {
                int status=0;
                char *line;
                int len;
                TOK *expr=NULL;
                LTV *tok_data;

                STRY(!tok,"validating file tok");
                if (CLL_EMPTY(&tok->subtoks)) {
                    printf("anons:\n"), print_ltvs(&context->anons,0);
                    edict_graph(edict);
                    STRY(!(tok_data=LTV_get(&tok->ltvs,KEEP,HEAD,NULL,0,NULL)),"validating file");
                    if (stdin==(FILE *) tok_data->data) { printf(CODE_BLUE "j2> " CODE_RESET); fflush(stdout); }
                    TRY((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,TRY_ERR,close_file,"reading from file");
                    TRY(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),TRY_ERR,free_line,"allocating expr tok");
                    TRY(!tokpush(&tok->subtoks,expr),TRY_ERR,free_expr,"enqueing expr token");
                    goto done; // success

                    free_expr:  TOK_free(expr);
                    free_line:  free(line);
                    close_file: fclose((FILE *) tok_data->data);
                    TOK_free((TOK *) CLL_cut(&tok->lnk));
                }
                else {
                    STRY(eval_tok((TOK *) CLL_get(&tok->subtoks,KEEP,HEAD)),"evaluating file subtoks");
                }

                done:
                return status;
            }

            STRY(!tok,"testing for null tok");

            switch(tok->flags&TOK_TYPES)
            {
                case TOK_FILE: STRY(eval_file(tok),"evaluating file"); break;
                case TOK_EXPR: STRY(eval_expr(tok),"evaluating expr"); break;
                case TOK_ATOM: STRY(eval_atom(tok),"evaluating atom"); break;
                default: TOK_free((TOK *) CLL_cut(&tok->lnk)); break;
            }

            done:
            return status;
        }

        STRY(!context,"testing for null context");

        if (eval_tok((TOK *) CLL_get(&context->toks,KEEP,HEAD)))
            CONTEXT_free((CONTEXT *) CLL_cut(&context->lnk));

        done:
        return status;
    }

    try_reset();
    while (!eval_context((CONTEXT *) CLL_ROT(&edict->contexts,FWD)));

 done:
    return status;
}




///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// Initialization
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

int edict_init(EDICT *edict)
{
    int status=0;
    LTV *root=LTV_new("ROOT",TRY_ERR,0);
    LT_init();
    STRY(!edict,"validating arg edict");
    BZERO(*edict);
    STRY(!LTV_enq(CLL_init(&edict->dict),root,HEAD),"pushing edict->dict root");
    CLL_init(&edict->contexts);
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,LTV_new((void *) stdin,sizeof(FILE *),LT_IMM)));
    STRY(!LTV_enq(&context->dict,root,HEAD),"pushing context->dict root");
    STRY(!CLL_put(&edict->contexts,&context->lnk,HEAD),"pushing edict's initial context");
    STRY(!CLL_init(&tok_repo),"initializing tok_repo");

 done:
    return status;
}

int edict_destroy(EDICT *edict)
{
    int status=0;
    STRY(!edict,"validating arg edict");
    CLL_release(&edict->dict,LTVR_release);
    CLL_release(&edict->contexts,CONTEXT_release);
 done:
    return status;
}
