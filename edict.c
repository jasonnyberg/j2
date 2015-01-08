
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
#include <stdlib.h>
#include "util.h"
#include "edict.h"

int debug_dump=0;
int prompt=1;

CLL tok_repo;
int tok_count=0;

//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

typedef enum {
    TOK_NONE     =0,

    // actionables
    TOK_FILE     =1<<0x00,
    TOK_EXPR     =1<<0x01,
    TOK_ATOM     =1<<0x02,
    TOK_LIT      =1<<0x03,

    TOK_EXEC     =1<<0x04,
    TOK_SCOPE    =1<<0x05,
    TOK_CURLY    =1<<0x06,
    TOK_TYPES    =TOK_FILE | TOK_EXPR | TOK_ATOM | TOK_LIT,
} TOK_FLAGS;

typedef struct TOK {
    CLL lnk;
    CLL ltvrs;
    CLL subtoks;
    TOK_FLAGS flags;
} TOK;

typedef struct CONTEXT {
    CLL lnk;
    CLL dict;  // cll of ltvr
    CLL anons; // cll of ltvr
    CLL toks;  // cll of tok
} CONTEXT;


TOK *tokpush(CLL *lst,TOK *tok) { return (TOK *) CLL_put(lst,&tok->lnk,HEAD); }
TOK *tokpop(CLL *lst)           { return (TOK *) CLL_get(lst,POP,HEAD); }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    TOK *tok=NULL;
    if (ltv && (tok=tokpop(&tok_repo)) || (tok=NEW(TOK)))
    {
        CLL_init(&tok->lnk);
        CLL_init(&tok->ltvrs);
        CLL_init(&tok->subtoks);
        tok_count++;
        tok->flags=flags;
        LTV_enq(&tok->ltvrs,ltv,HEAD);
    }
    return tok;
}

void TOK_free(TOK *tok)
{
    TOK *subtok;
    while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
    CLL_release(&tok->ltvrs,LTVR_release);
    tokpush(&tok_repo,tok);
    tok_count--;
}

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


void show_tok(TOK *tok);
void show_toks(char *pre,CLL *toks,char *post)
{
    void *op(CLL *lnk) { show_tok((TOK *) lnk); return NULL; }
    if (pre) printf("%s",pre);
    CLL_map(toks,FWD,op);
    if (post) printf("%s",post);
}

void show_tok(TOK *tok) {
    if (tok->flags&TOK_EXEC)   printf("EXEC " );
    if (tok->flags&TOK_SCOPE)  printf("SCOPE ");
    if (tok->flags&TOK_CURLY)  printf("CURLY ");
    if (tok->flags&TOK_FILE)   { printf("FILE \n"); print_ltvs(&tok->ltvrs,1); }
    if (tok->flags&TOK_EXPR)   { printf("EXPR \n"); print_ltvs(&tok->ltvrs,1); }
    if (tok->flags&TOK_ATOM)   { printf("ATOM \n"); print_ltvs(&tok->ltvrs,1); }
    if (tok->flags==TOK_NONE)  printf("_ ");
    else show_toks("(",&tok->subtoks,")\n");
}

int edict_graph(EDICT *edict) {
    int status=0;
    FILE *dumpfile;

    void graph_lti(LTI *lti,int depth,int *halt) {
        fprintf(dumpfile,"\t%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn)) fprintf(dumpfile,"\t%d -> %d [color=blue]\n",rb_parent(&lti->rbn),&lti->rbn);
        fprintf(dumpfile,"%d [label=\"\" shape=point color=red]\n",&lti->ltvrs);
        fprintf(dumpfile,"%d -> %d [weight=2]\n",&lti->rbn,&lti->ltvrs);
        fprintf(dumpfile,"%d -> %d [color=red]\n",&lti->ltvrs,lti->ltvrs.lnk[0]);
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
        else
            fprintf(dumpfile,"%d [label=\"\" shape=box style=filled height=.1 width=.3]\n",ltv);

        if (ltv->sub.ltis.rb_node)
            fprintf(dumpfile,"%1$d -> %2$d [color=blue lhead=cluster_%2$d]\n\n",ltv,ltv->sub.ltis.rb_node);
    }

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *halt) {
        if (*lti)  graph_lti(*lti,depth,halt);
        if (*ltvr) graph_ltvr(*ltvr,depth,halt);
        if (*ltv)  graph_ltv(*ltv,depth,halt);
        return NULL;
    }

    void descend_ltvr(LTVR *ltvr) { listree_traverse(ltvr->ltv,preop,NULL); }
    void descend_ltvrs(CLL *dict) {
        void *op(CLL *lnk) { descend_ltvr((LTVR *) lnk); return NULL; }
        CLL_map(dict,FWD,op);
    }

    void show_context(CONTEXT *context) {
        int halt=0;
        fprintf(dumpfile,"%1$d [label=\"Context-%1$d\" %1$d -> %2$d\n",context,&context->anons);
        fprintf(dumpfile,"%1$d [label=\"anons\" color=blue] %1$d -> %2$d\n",&context->anons,context->anons.lnk[0]);
        graph_ltvr((LTVR *) context->anons.lnk[0],0,&halt);
        descend_ltvrs(&context->dict);
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
    fprintf(dumpfile,"digraph iftree\n{\n\tnode [shape=record]\n\tedge []\n");

    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"ltv_count [label=\"ltv_count %d\"]\n",ltv_count);
    fprintf(dumpfile,"ltvr_count [label=\"ltvr_count %d\"]\n",ltvr_count);
    fprintf(dumpfile,"lti_count [label=\"lti_count %d\"]\n",lti_count);
    fprintf(dumpfile,"%1$d [label=\"dict\" color=blue] %1$d -> %2$d\n",&edict->dict,edict->dict.lnk[0]);

    //descend_ltvrs(&edict->dict);
    show_contexts("",&edict->contexts,"\n");

    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

done:
    return status;
}

#define OPS "@/?!$&|"
#define ATOM_END (OPS WHITESPACE "<({")

int parse(TOK *expr)
{
    int status=0;
    char *edata=NULL;
    int elen=0,tlen=0;
    LTV *val=NULL;

    int advance(x) { x=MIN(x,elen); edata+=x; elen-=x; return x; }

    int append(int type,char *data,int len,int adv) {
        TOK *tok=NULL;
        advance(adv);
        return !(tok=TOK_new(type,LTV_new(data,len,LT_DUP))) || !CLL_splice(&expr->subtoks,&tok->lnk,TAIL);
    }

    if (expr && (val=LTV_deq(&expr->ltvrs,HEAD)) && !(val->flags&LT_NSTR) && val->data) {
        edata=val->data;
        elen=val->len;

        while (elen>0) {
            switch (*edata) {
                case '\\': advance(1);
                case ' ':
                case '\t':
                case '\n': tlen=series(edata,elen,WHITESPACE,NULL,NULL); advance(tlen); break;
                case '<':  tlen=series(edata,elen,NULL,NULL,"<>");    STRY(append(TOK_EXPR|TOK_SCOPE,edata+1,tlen-2,tlen),"appending scope"); break;
                case '(':  tlen=series(edata,elen,NULL,NULL,"()");    STRY(append(TOK_EXPR|TOK_EXEC, edata+1,tlen-2,tlen),"appending exec"); break;
                case '{':  tlen=series(edata,elen,NULL,NULL,"{}");    STRY(append(TOK_EXPR|TOK_CURLY,edata+1,tlen-2,tlen),"appending curly"); break;
                case '[':  tlen=series(edata,elen,NULL,NULL,"[]");    STRY(append(TOK_LIT,edata+1,tlen-2,tlen),"appending lit"); break;
                default:   tlen=series(edata,elen,OPS,ATOM_END,NULL); STRY(append(TOK_ATOM,edata,tlen,tlen),"appending atom"); break;
            }
        }
    }
    else status=-1;

    done:
    return status;
}

#define LTV_NIL  LTV_new(NULL,0,LT_NIL)
#define LTV_NULL LTV_new(NULL,0,LT_NULL)
// FIXME: name idea: "Wrangle"

int edict_eval(EDICT *edict)
{
    int status=0;

    int eval_context(CONTEXT *context) {
        int status=0;
        TOK *tok;
        int exit_expr=0;

        int eval_tok(TOK *tok) {
            int eval_lit(TOK *tok) {
                int status=0;
                STRY(!LTV_enq(&context->anons,LTV_deq(&tok->ltvrs,HEAD),HEAD),"evaluating lit");
                TOK_free((TOK *) CLL_cut(&tok->lnk));
                done:
                return status;
            }

            // FIXME: convert all these inner functions to use TRY/STRY
            int eval_atom(TOK *tok) {
                int resolve_descend(LTVR *ltvr,char op,char *data,int len) {
                    int test_exit(LTV *ltv) {
                        int nil=(!ltv || ltv->flags&LT_NIL);
                        exit_expr=(op=='&')?nil:!nil;
                    }

                    int descend_ltvr(LTVR *ltvr,char *data,int len) {
                        int descend_lti(LTI *lti,char *data,int len,int reverse) {
                            int status=0;
                            LTVR *ltvr=NULL;
                            LTVR *resolve_ltvr(char *match,int matchlen,int insert) {
                                if (LTV_get(&lti->ltvrs,KEEP,reverse,match,matchlen,&ltvr))
                                    return ltvr;
                                if (op=='@' && insert) {
                                    LTV_put(&lti->ltvrs,matchlen?LTV_new(match,matchlen,LT_DUP):LTV_NULL,reverse,&ltvr);
                                    return ltvr;
                                }
                            }

                            int process_ltvr(char *data,int len) {
                                if (!len) {
                                    LTV *ltv=NULL;
                                    switch (op) {
                                        case '@': if (!ltvr) LTV_put(&lti->ltvrs,(ltv=LTV_deq(&context->anons,HEAD))?ltv:LTV_NIL,reverse,&ltvr); return 1;
                                        case '/': return (ltvr || resolve_ltvr(NULL,0,false))?LTVR_release(&ltvr->lnk),1:0;
                                        case '?': if (ltvr) print_ltv(ltvr->ltv,0); else print_ltvs(&lti->ltvrs,0); return 1;
                                        case '&':
                                        case '|': return test_exit((ltvr || resolve_ltvr(NULL,0,false))?ltvr->ltv:NULL),1;
                                        case '!': return 1; // exec w/ref unhandled for now
                                        default:  return LTV_enq(&context->anons,(ltvr || resolve_ltvr(NULL,0,false))?ltvr->ltv:LTV_NIL,HEAD),1;
                                        break;
                                    }
                                }
                                else if (data[0]=='.')
                                    return (ltvr || resolve_ltvr(NULL,0,true)) && descend_ltvr(ltvr,data+1,len-1);
                                else return test_exit(NULL),1;
                            }

                            if (!lti) { if (!op) LTV_enq(&context->anons,LTV_NIL,HEAD); return 1; }
                            else if (len && data[0]=='[') {
                                int done=0,tlen=series(data,len,NULL,".",NULL);
                                char *ref=data+tlen;
                                int reflen=len-tlen;
                                for (; (tlen=series(data,len,NULL,NULL,"[]")); data+=tlen,len-=tlen)
                                    if (resolve_ltvr(data+1,tlen-2,true))
                                        done|=process_ltvr(ref,reflen);
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

                        if ((tlen=series(data,len,NULL,".[",NULL))) {
                            lti=RBR_find(&ltv->sub.ltis,data,tlen,(op=='@'));
                            done=descend_lti(lti,data+tlen,len-tlen,reverse);
                            if (lti && CLL_EMPTY(&lti->ltvrs))
                                RBN_release(&ltv->sub.ltis,&lti->rbn,LTI_release);
                        }
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
                    void *descend(CLL *lnk) { return resolve_descend((LTVR *) lnk,op,data,len)?NULL+1:NULL; }
                    return CLL_map(&context->dict,FWD,descend)!=NULL;
                }

                int status=0;
                LTV *tok_ltv=NULL;

                STRY(!(tok_ltv=LTV_get(&tok->ltvrs,KEEP,HEAD,NULL,0,NULL)),"retrieving atom tok data");

                char *data=tok_ltv->data;
                int i,len=tok_ltv->len;
                int tlen=series(data,len,OPS,NULL,NULL);

                for (i=0; (!i || i<tlen) && !exit_expr; i++)
                    resolve(tlen?data[i]:0,data+tlen,len-tlen);

                done:
                TOK_free((TOK *) CLL_cut(&tok->lnk));
                return status;
            }


            int eval_expr(TOK *tok) {
                int status=0;

                if (CLL_EMPTY(&tok->subtoks)) {
                    if (parse(tok)) TOK_free((TOK *) CLL_cut(&tok->lnk)); // empty expr
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
                    STRY(!(tok_data=LTV_get(&tok->ltvrs,KEEP,HEAD,NULL,0,NULL)),"validating file");
                    if (stdin==(FILE *) tok_data->data) { printf(CODE_BLUE "j2> " CODE_RESET); fflush(stdout); }
                    TRY((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,-1,close_file,"reading from file");
                    TRY(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),-1,free_line,"allocating expr tok");
                    TRY(!tokpush(&tok->subtoks,expr),-1,free_expr,"enqueing expr token");
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
                case TOK_LIT:  STRY(eval_lit(tok), "evaluating lit");  break;
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

int edict_init(EDICT *edict,LTV *root)
{
    int status=0;
    LT_init();
    STRY(!edict,"validating arg edict");
    BZERO(*edict);
    STRY(!LTV_enq(CLL_init(&edict->dict),root,HEAD),"pushing edict->dict root");
    CLL_init(&edict->contexts);
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,LTV_new((void *) stdin,sizeof(FILE *),LT_IMM)));
    STRY(!LTV_enq(&context->dict,root,HEAD),"pushing context->dict root");
    STRY(!CLL_put(&edict->contexts,&context->lnk,HEAD),"pushing edict's initial context")
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
