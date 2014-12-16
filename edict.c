
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

static char *ELLIPSIS="...";
static char *ANONYMOUS="";

//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

typedef enum {
    TOK_NONE     =0,

    // actionables
    TOK_FILE     =1<<0x00,
    TOK_EXPR     =1<<0x01,
    TOK_LIT      =1<<0x02,
    TOK_OP       =1<<0x03,
    TOK_REF      =1<<0x04,

    TOK_ADD      =1<<0x05,
    TOK_EXEC     =1<<0x06,
    TOK_SCOPE    =1<<0x07,
    TOK_CURLY    =1<<0x08,
    TOK_CONT     =1<<0x09,
    TOK_LINK     =1<<0x0a,
    TOK_TYPES    = TOK_FILE | TOK_EXPR | TOK_LIT | TOK_OP | TOK_REF,
} TOK_FLAGS;


typedef struct TOK {
    CLL cll;
    TOK_FLAGS flags;
    char *data;
    int len;
    CLL subtoks;
    LTI *lti;
    LTVR *ltvr;
} TOK;

typedef struct CONTEXT {
    CLL cll;
    char *line;
    CLL dict;  // cll of ltvr
    CLL anons; // cll of ltvr
    CLL toks;  // cll of tok
    CLL uncommitted; // CLL of io-bound anons
} CONTEXT;


TOK *tokpush(CLL *lst,TOK *tok) { return (TOK *) CLL_put(lst,&tok->cll,HEAD); }
TOK *tokpop(CLL *lst)           { return (TOK *) CLL_get(lst,POP,HEAD); }

TOK *TOK_new(TOK_FLAGS flags,char *data,int len)
{
    TOK *tok=NULL;
    if ((tok=tokpop(&tok_repo)) || (tok=NEW(TOK)))
    {
        CLL_init(&tok->cll);
        CLL_init(&tok->subtoks);
        tok_count++;
        tok->flags=flags;
        tok->data=data;
        tok->len=len;
    }
    return tok;
}

void TOK_free(TOK *tok)
{
    TOK *subtok;
    while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
    tokpush(&tok_repo,tok);
    tok_count--;
}

CONTEXT *CONTEXT_new(TOK *tok)
{
    CONTEXT *context=NULL;
    if (tok && (context=NEW(CONTEXT))) {
        CLL_init(&context->dict); CLL_init(&context->anons); CLL_init(&context->toks); CLL_init(&context->uncommitted);
        CLL_put(&context->toks,&tok->cll,HEAD);
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

void CONTEXT_release(CLL *cll) { CONTEXT_free((CONTEXT *) cll); }


void show_tok(TOK *tok);
void show_toks(char *pre,CLL *cll,char *post)
{
    void *op(CLL *lnk) { show_tok((TOK *) lnk); return NULL; }
    if (pre) printf("%s",pre);
    CLL_map(cll,FWD,op);
    if (post) printf("%s",post);
}

void show_tok(TOK *tok) {
    if (tok->flags&TOK_ADD)     printf("ADD ");
    if (tok->flags&TOK_CONT)    printf("CONT ");
    if (tok->flags&TOK_EXEC)    printf("EXEC " );
    if (tok->flags&TOK_SCOPE)   printf("SCOPE ");
    if (tok->flags&TOK_CURLY)   printf("CURLY ");
    if (tok->flags&TOK_FILE)    printf("FILE "), fprintf(stdout,"0x%x",tok->data),putchar(' ');
    if (tok->flags&TOK_EXPR)    printf("EXPR "), fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_LIT)     printf("LIT "),  fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_OP)      printf("OP "),   fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_REF)     printf("REF "),  fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags==TOK_NONE)   printf("_ ");
    else show_toks("(",&tok->subtoks,")");
}

void show_context(CONTEXT *context,FILE *file);
void show_contexts(char *pre,CLL *cll,char *post,FILE *file)
{
    void *op(CLL *lnk) { show_context((CONTEXT *) lnk,file); return NULL; }
    if (pre) printf("%s",pre);
    CLL_map(cll,FWD,op);
    if (post) printf("%s",post);
}
void show_context(CONTEXT *context,FILE *file) {
    fprintf(file,"Context:\n");
    fprintf(file,"%1$d [label=\"anon\" color=blue] %1$d -> %2$d\n",&context->anons,context->anons.lnk[0]);
    fprintf(file,"%1$d [label=\"toks\" color=blue] %1$d -> %2$d\n",&context->toks,context->toks.lnk[0]);
}


#define LTV_NIL (LTV_new(NULL,0,LT_NIL))

int edict_eval(EDICT *edict)
{
    int status=0;
    char *bc_ws=CONCATA(bc_ws,edict->bc,WHITESPACE);

    int eval_context(CONTEXT *context) {
        int status=0;
        TOK *cur_expr=NULL;

        TOK *eval_tok(TOK *tok) {
            char op;

            // FIXME: name idea: "Wrangle"

            TOK *eval_op_ref(TOK *tok) {
                int status=0;
                TOK *result=NULL;

                int insert=tok->flags&TOK_ADD;
                int tail=0;
                char *name=NULL;
                int namelen=0;

                int resolve_ltvr() {
                    return (result && result->lti && // parent name was resolved, and...
                            (result->ltvr || // a) parent value already obtained, or
                            LTV_get(&result->lti->cll,KEEP,tail,NULL,0,&result->ltvr) || // b) lookup succeeded, or
                            (insert && LTV_put(&result->lti->cll,LTV_NIL,tail,&result->ltvr)) // c) placeholder inserted
                            )
                           );
                }

                void *resolve_lti(CLL *cll) { return RBR_find(&((LTVR *) cll)->ltv->rbr,name,namelen,insert); }

                void *descend(CLL *lnk) {
                    TOK *curtok=(TOK *) lnk;

                    printf("eval_op_ref:");

                    if (curtok->flags&TOK_OP) // ignore ADD/REV flags
                    {
                        show_tok(curtok);


                         result=curtok;
                    }
                    else if (curtok->flags&TOK_REF)
                    {
                        tail=(curtok->len && curtok->data[0]=='-');
                        name=curtok->data+tail;
                        namelen=curtok->len-tail;

                        show_tok(curtok);

                        if (result) { if (resolve_ltvr()) curtok->lti=(LTI *) resolve_lti(&result->ltvr->cll); }
                        else curtok->lti=CLL_map(&context->dict,FWD,resolve_lti);
                        result=curtok->lti?curtok:NULL;
                    }
                    else if (curtok->flags&TOK_LIT)
                    {
                        show_tok(curtok);
                        if (result) { // subscript lit
                            if (result->lti && !LTV_get(&result->lti->cll,0,tail,curtok->data,curtok->len,&result->ltvr) && tok->flags&TOK_ADD)
                                LTV_put(&result->lti->cll,LTV_new(curtok->data,curtok->len,cur_expr->flags&TOK_LINK?LT_DUP:0),tail,&result->ltvr);
                            curtok->data=result->data;
                            curtok->lti=result->lti;
                        }
                        else { // simple lit
                            LTV *anon=LTV_new(curtok->data,curtok->len,LT_ESC);
                            LTV_put(&context->anons,anon,HEAD,NULL);
                            LTV_put(&context->uncommitted,anon,HEAD,NULL);
                            TOK_free((TOK *) CLL_cut(&curtok->cll));
                            curtok=NULL;
                        }
                        result=curtok && curtok->lti?curtok:NULL;
                    }
                    printf("\n");

                    curtok->flags|=TOK_CONT;

                    if (CLL_EMPTY(&curtok->subtoks))
                    {
                        printf("eval_op finalize\n");
                        show_tok(curtok);

                        if (tok->flags&TOK_OP)
                        {
                            LTV *ltv=NULL;
                            int i;
                            for (i=0;i<tok->len;i++) switch (tok->data[i])
                            {
                                case '?':
                                    print_ltvs(&edict->dict,0);
                                    break;
                                case '@':
                                    if (result && result->lti) LTV_put(&result->lti->cll,(ltv=LTV_pop(&context->anons))?ltv:LTV_NIL,tail,&result->ltvr);
                                    break;
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
                                        TOK *lambda_tok=TOK_new(TOK_EXPR,lambda->data,lambda->len);
                                        LTV_release(lambda);
                                        CLL_put(&cur_expr->subtoks,&lambda_tok->cll,HEAD);
                                    }
                                    break;
                                default:
                                    printf("processing unimplemented OP %c",tok->data[i]);
                            }
                        }
                        else if (resolve_ltvr())
                            return LTV_push(&context->anons,result->ltvr->ltv);
                    }
                    else
                        return CLL_map(&curtok->subtoks,FWD,descend);

                    return NULL;
                }

                result=descend(&tok->cll);
                if (!result) {
                    TOK_free((TOK *) CLL_cut(&tok->cll));
                    tok=NULL;
                }
                done:
                return result;
            }

            TOK *eval_expr(TOK *expr) {
                int status=0;
                TOK *result=NULL;
                cur_expr=expr;

                int parse()
                {
                    int status=0;
                    TOK *curtok=NULL;
                    char *edata=NULL;
                    int elen=0;
                    int flags=expr->flags&TOK_LINK;

                    void advance(adv) { adv=MIN(adv,elen); edata+=adv; elen-=adv; }

                    void append(int reset,int type,char *data,int len,int adv) {
                        advance(adv);
                        if (curtok && curtok->flags&TOK_OP && type&TOK_OP)
                            curtok->flags|=flags,curtok->len++;
                        else {
                            TOK *tok=TOK_new(type|flags,data,len);
                            flags=expr->flags&TOK_LINK;
                            if (reset || (curtok && !(curtok->flags&TOK_OP) && (tok->flags&TOK_OP)))
                                curtok=NULL;
                            curtok=(TOK *) CLL_splice(curtok?&curtok->subtoks:&expr->subtoks,&tok->cll,TAIL);
                            if (reset)
                                curtok=NULL;
                        }
                    }

                    if (expr && expr->data)
                    {
                        int tlen;
                        edata=expr->data;
                        elen=expr->len;

                        while (elen>0) {
                            switch(*edata) {
                                case '\\': advance(1);
                                case ' ':
                                case '\t':
                                case '\n': tlen=series(edata,elen,WHITESPACE,NULL,NULL); advance(tlen); curtok=NULL; break;
                                case '<':  tlen=series(edata,elen,NULL,NULL,"<>"); append(1,TOK_EXPR|TOK_SCOPE,edata+1,tlen-2,tlen); break;
                                case '(':  tlen=series(edata,elen,NULL,NULL,"()"); append(1,TOK_EXPR|TOK_EXEC, edata+1,tlen-2,tlen); break;
                                case '{':  tlen=series(edata,elen,NULL,NULL,"{}"); append(1,TOK_EXPR|TOK_CURLY,edata+1,tlen-2,tlen); break;
                                case '[':  tlen=series(edata,elen,NULL,NULL,"[]"); append(0,TOK_LIT,           edata+1,tlen-2,tlen); break;
                                case '!':  append(0,TOK_OP|TOK_EXEC,edata,1,1); break;
                                case '@':  append(0,TOK_OP|TOK_ADD,edata,1,1); break;
                                case '/':
                                case '$':
                                case '&':
                                case '|':
                                case '?':  append(0,TOK_OP,edata,1,1); break;
                                case '.':  tlen=series(edata,elen,".",NULL,NULL);
                                    switch(tlen) {
                                        case 1:  advance(tlen); break;
                                        case 2:  append(0,TOK_REF,ANONYMOUS,0,tlen); break;
                                        default: append(0,TOK_REF,ELLIPSIS,3,tlen); break;
                                    }
                                    break;
                                default: tlen=series(edata,elen,NULL,bc_ws,NULL); if (tlen) append(0,TOK_REF,edata,tlen,tlen); break;
                            }
                        }
                    }

                    return elen;
                }

                if (expr->flags&TOK_CONT) // already parsed
                {
                    if (CLL_EMPTY(&expr->subtoks))
                    {
                        if (expr->flags&TOK_EXEC || expr->flags&TOK_SCOPE) // nothing special for curly yet
                        {
                            LTV *ltv=NULL;
                            STRY(!(ltv=LTV_pop(&context->dict)),"popping scope");
                            STRY((LTV_release(ltv),0),"releasing scope");
                        }
                        TOK_free((TOK *) CLL_cut(&expr->cll));
                        expr=NULL;
                    }
                    else
                    {
                        result=eval_tok((TOK *) CLL_get(&expr->subtoks,KEEP,HEAD));
                    }
                }
                else {
                    if (expr->flags&TOK_SCOPE)
                    {
                        STRY(!LTV_push(&context->dict,LTV_pop(&context->anons)),"pushing scope");
                    }
                    else if (expr->flags&TOK_EXEC)
                    {
                        LTV *lambda=NULL;
                        TOK *lambda_tok=NULL;
                        STRY(!(lambda=LTV_pop(&context->anons)),"popping lambda");
                        STRY(!(expr->ltvr=LTVR_new(lambda)),"recording lambda");
                        STRY(!LTV_push(&context->dict,lambda),"pushing lambda scope");
                        STRY(!(lambda_tok=TOK_new(TOK_EXPR,lambda->data,lambda->len)),"pushing lambda expr");
                        tokpush(&expr->subtoks,lambda_tok);
                    }

                    STRY(parse(),"parse expr");
                    expr->flags|=TOK_CONT;
                }

                done:
                return result;
            }


            TOK *eval_file(TOK *file_tok) {
                int status=0;
                int len;
                TOK *result=NULL;

                if (CLL_EMPTY(&file_tok->subtoks))
                {
                    if (context->line)
                    {
                        LTV *ltv=NULL;
                        while (ltv=LTV_get(&context->uncommitted,POP,HEAD,NULL,0,0)) LTV_commit(ltv);
                        free(context->line);
                    }
                    STRY(!file_tok->data,"validating file");
                    TRY((context->line=balanced_readline((FILE *) file_tok->data,&len))==NULL,-1,read_failed,"reading from file");
                    TRY(!tokpush(&file_tok->subtoks,TOK_new(TOK_EXPR|TOK_LINK,context->line,len)),-1,tok_failed,"allocating file-sourced expr token");
                }
                else
                {
                    TRY(!(result=eval_tok((TOK *) CLL_get(&file_tok->subtoks,KEEP,HEAD))),-1,tok_failed,"evaluating file subtoks");
                }
                goto done; // success!

                tok_failed:
                free(context->line);
                read_failed:
                fclose((FILE *) file_tok->data);
                TOK_free((TOK *) CLL_cut(&file_tok->cll));
                file_tok=NULL;

                done:
                return file_tok;
            }

            TOK *result=NULL;

            switch(tok->flags&TOK_TYPES)
            {
                case TOK_NONE: TOK_free(tok);           break;
                case TOK_EXPR: result=eval_expr(tok);   break;
                case TOK_FILE: result=eval_file(tok);   break;
                case TOK_LIT:  result=eval_op_ref(tok); break;
                case TOK_OP:   result=eval_op_ref(tok); break;
                case TOK_REF:  result=eval_op_ref(tok); break;
                default:       status=-1;               break;
            }

            done:
            return result;
        }

        STRY(!context,"testing for null context");
        show_toks("toks: ",(CLL *) &context->toks,"\n");
        printf("anons:\n"), print_ltvs(&context->anons,0);
        printf("uncommitted:\n"), print_ltvs(&context->uncommitted,0);
        printf("line: %s\n",context->line);

        if (eval_tok((TOK *) CLL_get(&context->toks,KEEP,HEAD)))
            CLL_put(&edict->contexts,&context->cll,TAIL);
        else
            CONTEXT_free(context);

        done:
        return status;
    }

    try_reset();

    CONTEXT *context=NULL;
    while((context=(CONTEXT *) CLL_get(&edict->contexts,FWD,POP)))
        STRY(eval_context(context),"evaluating context");

    /*  REDESIGN:
     * one context per thread, which tracks: expression stack, anonymous values, context stack (dict namespace/function nesting)
     * each dict entry should track owner/occupier id
     * threads can't modify dict items that it doesn't own AND occupy. (can't add OR delete)
     * atoms keep track of their ref-stacks.

        eval_ref(expr,tok)
            ...;

        eval_ops(expr,tok)
            ...
                return tok;

        eval_tok(expr,tok) // pop tok when complete
            eval_ops(expr,eval_ref(expr,tok))

        eval_expr(context,expr) // pop expr when complete
            eval_tok(context,expr,cll_tos(expr->toks))

        eval_context(context) // pop context when complete
            eval_expr(context,cll_tos(context->exprs))
    */

 done:
    return status;
}




///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// Initialization
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////


int bc_dummy(EDICT *edict,char *ref,int len)
{
    return 1;
}

int edict_bytecode(EDICT *edict,char bc,edict_bc_impl bcf)
{
    edict->bc[edict->numbc++]=bc;
    edict->bc[edict->numbc]=0;
    edict->bcf[bc]=bcf;
    return 0;
}

int edict_bytecodes(EDICT *edict)
{
    int i;

    for (i=0;i<256;i++)
        edict->bcf[i]=0;

    edict_bytecode(edict,'[',bc_dummy); // lit
    edict_bytecode(edict,'.',bc_dummy); // namesep
    edict_bytecode(edict,'@',bc_dummy); // assign
    edict_bytecode(edict,'/',bc_dummy); // deassign/free
    edict_bytecode(edict,'<',bc_dummy); // scope open
    edict_bytecode(edict,'>',bc_dummy); // scope close
    edict_bytecode(edict,'(',bc_dummy); // exec_enter same as namespace_enter
    edict_bytecode(edict,')',bc_dummy); // exec exit
    edict_bytecode(edict,'{',bc_dummy); // ??? open
    edict_bytecode(edict,'}',bc_dummy); // ??? close
    edict_bytecode(edict,'+',bc_dummy); // add immediate lit if neccessary
    edict_bytecode(edict,'-',bc_dummy); // reverse
    edict_bytecode(edict,'!',bc_dummy); // bc_map
    edict_bytecode(edict,'&',bc_dummy); // bc_and
    edict_bytecode(edict,'|',bc_dummy); // bc_or
    edict_bytecode(edict,'?',bc_dummy); // bc_print
    edict_bytecode(edict,'$',bc_dummy); // bc_name
    edict_bytecode(edict,'#',bc_dummy); // bc_index
    return 0;
}

int edict_init(EDICT *edict,LTV *root)
{
    int status=0;
    LT_init();
    STRY(!edict,"validating arg edict");
    BZERO(*edict);
    STRY(!LTV_push(CLL_init(&edict->dict),root),"pushing edict->dict root");
    CLL_init(&edict->contexts);
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,(void *) stdin,sizeof(FILE *)));
    STRY(!LTV_push(&context->dict,root),"pushing context->dict root");
    STRY(!CLL_put(&edict->contexts,&context->cll,HEAD),"pushing edict's initial context")
    STRY(!CLL_init(&tok_repo),"initializing tok_repo");
    STRY(edict_bytecodes(edict),"initializing bytecodes");

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
