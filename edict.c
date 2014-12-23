
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
        LTV_push(&tok->ltvrs,ltv);
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

void show_context(CONTEXT *context,FILE *file);
void show_contexts(char *pre,CLL *contexts,char *post,FILE *file)
{
    void *op(CLL *lnk) { show_context((CONTEXT *) lnk,file); return NULL; }
    if (pre) printf("%s",pre);
    CLL_map(contexts,FWD,op);
    if (post) printf("%s",post);
}
void show_context(CONTEXT *context,FILE *file) {
    fprintf(file,"Context:\n");
    fprintf(file,"%1$d [label=\"anon\" color=blue] %1$d -> %2$d\n",&context->anons,context->anons.lnk[0]);
    fprintf(file,"%1$d [label=\"toks\" color=blue] %1$d -> %2$d\n",&context->toks,context->toks.lnk[0]);
}


#define OPS "!@/$&|?"
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

    if (expr && (val=LTV_pop(&expr->ltvrs)) && !(val->flags&LT_NSTR) && val->data) {
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

// FIXME: name idea: "Wrangle"

int edict_eval(EDICT *edict)
{
    int status=0;

    int eval_context(CONTEXT *context) {
        int status=0;
        TOK *tok;

        int eval_tok(TOK *tok) {
            int eval_lit(TOK *tok) {
                int status=0;
                printf("eval_lit: "); show_tok(tok);
                STRY(!LTV_push(&context->anons,LTV_pop(&tok->ltvrs)),"evaluating lit");
                TOK_free((TOK *) CLL_cut(&tok->lnk));
                done:
                return status;
            }

            int eval_atom(TOK *tok) {
                int status=0;
                printf("eval_atom: "); show_tok(tok);
                LTV *tok_ltv=NULL;

                if ((tok_ltv=LTV_get(&tok->ltvrs,KEEP,HEAD,NULL,0,NULL))) {
                    int insert=0,reverse=0;
                    char *data=tok_ltv->data;
                    int len=tok_ltv->len,tlen;
                    LTI *lti=NULL;
                    LTVR *ltvr=NULL;
                    LTV *ltv=NULL;
                    int advance(x) { x=MIN(x,len); data+=x; len-=x; return x; }

                    int oplen=series(data,len,OPS,NULL,NULL);
                    advance(oplen);

                    LTI *resolve(int insert) {
                        LTV *ltv=NULL;
                        void *op(CLL *lnk) {
                            if (lti) return lti;
                            ltv=((LTVR *) lnk)->ltv;
                            while ((tlen=series(data,len,NULL,".[",NULL))) {
                                tlen-=advance(reverse=(data[0]=='-'));
                                ltvr=NULL;
                                if (!(lti=RBR_find(&ltv->sub.ltis,data,tlen,insert)))
                                    return NULL;
                                advance(tlen);
                                while (tlen=series(data,len,NULL,NULL,"[]")) {
                                    if (!(ltv=LTV_get(&lti->ltvrs,KEEP,reverse,data+1,tlen-2,&ltvr)) &&
                                            insert &&
                                            !(ltv=LTV_put(&lti->ltvrs,LTV_new(data+1,tlen-2,LT_DUP),reverse,&ltvr)))
                                        return NULL;
                                    advance(tlen);
                                }
                                if (len) {
                                    if (*data!='.')
                                        return NULL;
                                    advance(1);
                                }
                            }
                            return lti;
                        }
                        return CLL_map(&context->dict,FWD,op);
                    }

                    if (oplen) {
                        int i;
                        char *ops=(char *) tok_ltv->data;
                        for (i=0; i<oplen; i++) {
                            switch (ops[i]) {
                                case '@': if (oplen!=tok_ltv->len && resolve(1)) LTV_put(&lti->ltvrs,(ltv=LTV_pop(&context->anons))?ltv:ltv_nil,reverse,&ltvr); break;
                                case '/':
                                    if (oplen==tok_ltv->len) LTV_release(LTV_pop(&context->anons));
                                    else if (resolve(0)) {
                                        if (ltvr) { ltv=ltvr->ltv; LTVR_release(&ltvr->lnk); ltvr=NULL; }
                                        LTV_release(ltv?ltv:LTV_get(&lti->ltvrs,POP,reverse,NULL,0,NULL));
                                    }
                                    break;
                                case '?': print_ltvs(resolve(0)? &lti->ltvrs:&context->dict,0);
                                case '!':
                                    if (resolve(0))
                                    {
                                        // use as arg for function call
                                    }
                                    else
                                    {
                                        LTV *lambda=LTV_pop(&context->anons);
                                        TOK *expr=TOK_new(TOK_EXPR,lambda);
                                        CLL_put(&tok->lnk,&expr->lnk,TAIL); // insert ahead of this token
                                    }
                                    break;
                                default:
                                    printf("processing unimplemented OP %c",ops[i]);
                            }
                        }
                    }
                    else if (resolve(0) && ltvr || LTV_get(&lti->ltvrs,KEEP,reverse,NULL,0,&ltvr)) LTV_push(&context->anons,ltvr->ltv);
                }

                if (!status)
                    TOK_free((TOK *) CLL_cut(&tok->lnk));

                done:
                return status;
            }


            int eval_expr(TOK *tok) {
                int status=0;
                printf("eval_expr: "); show_tok(tok);

                if (CLL_EMPTY(&tok->subtoks)) {
                    if (parse(tok)) TOK_free((TOK *) CLL_cut(&tok->lnk)); // empty expr
                    else if (tok->flags&TOK_SCOPE)
                        LTV_push(&context->dict,LTV_pop(&context->anons));
                    else if (tok->flags&TOK_EXEC) {
                        LTV *lambda=LTV_pop(&context->anons);
                        TOK *expr=TOK_new(TOK_EXPR,lambda);
                        STRY(!LTV_push(&context->dict,lambda),"pushing lambda scope");
                        STRY(!CLL_put(&tok->subtoks,&expr->lnk,TAIL),"pushing lambda expr");
                    }
                }
                else { // evaluate
                    STRY(eval_tok((TOK *) CLL_get(&tok->subtoks,KEEP,HEAD)),"evaluating expr subtoks");
                    if (CLL_EMPTY(&tok->subtoks)) {
                        if (tok->flags&(TOK_EXEC|TOK_SCOPE)) // nothing special for curly yet
                            LTV_release(LTV_pop(&context->dict));
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
                    STRY(!(tok_data=LTV_get(&tok->ltvrs,KEEP,HEAD,NULL,0,NULL)),"validating file");
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

        show_toks("toks: ",(CLL *) &context->toks,"\n");
        printf("anons:\n"), print_ltvs(&context->anons,0);

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
    STRY(!LTV_push(CLL_init(&edict->dict),root),"pushing edict->dict root");
    CLL_init(&edict->contexts);
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,LTV_new((void *) stdin,sizeof(FILE *),LT_IMM)));
    STRY(!LTV_push(&context->dict,root),"pushing context->dict root");
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
