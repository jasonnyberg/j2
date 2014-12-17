
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
    TOK_FLAGS flags;
    CLL ltvrs;
    CLL subtoks;
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
    if (tok->flags&TOK_FILE)   { printf("FILE "); print_ltvs(&tok->ltvrs,1); putchar(' '); }
    if (tok->flags&TOK_EXPR)   { printf("EXPR "); print_ltvs(&tok->ltvrs,1); putchar(' '); }
    if (tok->flags&TOK_ATOM)   { printf("ATOM "); print_ltvs(&tok->ltvrs,1); putchar(' '); }
    if (tok->flags==TOK_NONE)  printf("_ ");
    else show_toks("(",&tok->subtoks,")");
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



LTI *listree_op(LTV *ltv,char *name,int len,int insert)
{
    char *lit;
    int litlen;
    int reverse;

    int advance(x) { x=MIN(x,len); name+=x; len-=x; return x; }

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *halt)
    {
        int tlen=series(name,len,NULL,".[",NULL);
        switch (name[0]) {
            case '-': reverse=1; advance(1); break;
            case '.': advance(1); break;
            case '[': litlen=series(name,len,NULL,NULL,"[]"); lit=name+1; litlen=advance(litlen)-2; break;
            default: break;
        }

        if (*lti) { // list lookup/insert, get ltvr
            if (!LTV_get(&(*lti)->ltvrs,0,reverse,lit,litlen,ltvr) && insert)
                LTV_put(&(*lti)->ltvrs,LTV_new(lit,litlen,LT_DUP),reverse,ltvr);

        }
        if (*ltvr) {
        }
        if (*ltv) { // tree lookup/insert, get lti
            *lti=RBR_find(&(*ltv)->sub.ltis,name,tlen,insert);
            *halt=1;
            if (!(len-=tlen)) return lti;
            else if (insert)
            {}
        }

        return NULL;
    }

    listree_traverse(ltv,preop,NULL);
}


#define BAL "<({"
#define OPS "!@/$&|?"

int parse(TOK *expr)
{
    int status=0;
    char *edata=NULL;
    int elen=0,tlen=0;
    LTV *val=NULL;

    int advance(x) { x=MIN(x,elen); edata+=x; elen-=x; return x; }

    int append(int type,char *data,int len,int adv) {
        int status=0;
        TOK *tok=NULL;
        advance(adv);
        STRY(!(tok=TOK_new(type,LTV_new(data,len,LT_DUP))),"allocating expr subtok");
        STRY(!CLL_splice(&expr->subtoks,&tok->lnk,TAIL),"appending expr subtok");
        done:
        return status;
    }

    STRY(!expr,"validating expr");
    STRY(!(val=LTV_pop(&expr->ltvrs)),"getting expr value");
    STRY(!val->data,"validating expr value");

    edata=val->data;
    elen=val->len;

    while (elen>0) {
        switch(*edata) {
            case '\\': advance(1);
            case ' ':
            case '\t':
            case '\n': tlen=series(edata,elen,WHITESPACE,NULL,NULL); advance(tlen); break;
            case '<':  tlen=series(edata,elen,NULL,NULL,"<>");              STRY(append(TOK_EXPR|TOK_SCOPE,edata+1,tlen-2,tlen),"appending scope"); break;
            case '(':  tlen=series(edata,elen,NULL,NULL,"()");              STRY(append(TOK_EXPR|TOK_EXEC, edata+1,tlen-2,tlen),"appending exec"); break;
            case '{':  tlen=series(edata,elen,NULL,NULL,"{}");              STRY(append(TOK_EXPR|TOK_CURLY,edata+1,tlen-2,tlen),"appending curly"); break;
            case '[':  tlen=series(edata,elen,NULL,NULL,"[]");              STRY(append(TOK_LIT,edata+1,tlen-2,tlen),"appending lit"); break;
            default:   tlen=series(edata,elen,OPS,OPS BAL WHITESPACE,NULL); STRY(append(TOK_ATOM,edata,tlen,tlen),"appending atom"); break;
        }
    }

    done:
    LTV_free(val);
    return status;
}



#define LTV_NIL (LTV_new(NULL,0,LT_NIL))

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
                int tlen=0;

                printf("eval_atom: "); show_tok(tok);

                /*
                if (tok->flags&TOK_OP)
                {
                    LTV *ltv=NULL;
                    int i;
                    for (i=0;i<tok->len;i++) switch (tok->data[i])
                    {
                        case '?': print_ltvs(&edict->dict,0); break;
                        case '@': if (result && result->lti) LTV_put(&result->lti->cll,(ltv=LTV_pop(&context->anons))?ltv:LTV_NIL,tail,&result->ltvr); break;
                        case '/':
                            if (result && result->lti) LTV_release(LTV_get(&result->lti->cll,POP,tail,NULL,0,NULL));
                            else LTV_release(LTV_pop(&context->anons));
                            break;
                        case '!':
                            if (resolve_ltvr())
                            {
                                // use as arg for function call
                            }
                            else
                            {
                                LTV *lambda=LTV_pop(&context->anons);
                                                                TOK *lambda_tok=TOK_new(TOK_EXPR,LTV_new(data,len,LT_DUP));
                                LTV_release(lambda);
                                CLL_put(&cur_expr->subtoks,&lambda_tok->cll,HEAD);
                            }
                            break;
                        default:
                            printf("processing unimplemented OP %c",tok->data[i]);
                    }
                }
                else if (resolve_ltvr())
                    LTV_push(&context->anons,result->ltvr->ltv);
                */

                if (status)
                    TOK_free((TOK *) CLL_cut(&tok->lnk));

                done:
                return status;
            }



            int eval_expr(TOK *tok) {
                int status=0;
                printf("eval_expr: "); show_tok(tok);

                if (CLL_EMPTY(&tok->subtoks)) {
                    parse(tok);
                    if (tok->flags&TOK_SCOPE)
                        LTV_push(&context->dict,LTV_pop(&context->anons));
                    else if (tok->flags&TOK_EXEC)
                    {
                        LTV *lambda=LTV_pop(&context->anons);
                        TOK *expr=TOK_new(TOK_EXPR,lambda);
                        STRY(!LTV_push(&context->dict,lambda),"pushing lambda scope");
                        STRY(!CLL_put(&tok->subtoks,&expr->lnk,TAIL),"pushing lambda expr");
                    }
                }
                else {
                    STRY(eval_tok((TOK *) CLL_get(&tok->subtoks,KEEP,HEAD)),"evaluating expr subtoks");
                    if (CLL_EMPTY(&tok->subtoks) && tok->flags&(TOK_EXEC|TOK_SCOPE)) // nothing special for curly yet
                        LTV_release(LTV_pop(&context->dict));
                    TOK_free((TOK *) CLL_cut(&tok->lnk));
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
                STRY(!(tok_data=LTV_get(&tok->ltvrs,KEEP,HEAD,NULL,0,NULL)),"validating file");
                TRY((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,-1,close_file,"reading from file");
                TRY(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),-1,free_line,"allocating expr tok");
                TRY(!tokpush(&tok->subtoks,expr),-1,free_expr,"enqueing expr token");
                goto done; // success

                free_expr:  TOK_free(expr);
                free_line:  free(line);
                close_file: fclose((FILE *) tok_data->data);
                TOK_free((TOK *) CLL_cut(&tok->lnk));

                done:
                return status;
            }

            STRY(!tok,"testing for null tok");

            if (!CLL_EMPTY(&tok->subtoks))

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

        STRY(!(tok=(TOK *) CLL_get(&context->toks,KEEP,HEAD)),"retrieving tok");
        STRY(eval_tok(tok),"evaluating tok");
        STRY(!CLL_put(&edict->contexts,&context->lnk,TAIL),"rescheduling context");
        goto done; // success!

        CONTEXT_free(context); // uh oh

        done:
        return status;
    }

    try_reset();

    CONTEXT *context=NULL;
    while((context=(CONTEXT *) CLL_get(&edict->contexts,FWD,POP)))
        STRY(eval_context(context),"evaluating context");

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
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,LTV_new((void *) stdin,sizeof(FILE *),LT_BIN)));
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
