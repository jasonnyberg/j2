
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

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// PARSER
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

char *edict_read(FILE *ifile,int *exprlen)
{
    char *expr=NULL;
    
    char *nextline(int *len) {
        static char *line=NULL;
        static size_t buflen=0;

        if ((*len=getline(&line,&buflen,ifile))>0)
        {
            if ((expr=realloc(expr,(*exprlen)+(*len)+1)))
                memmove(expr+(*exprlen),line,(*len)+1);
            return expr;
        }
        return NULL;
    }

    int depth=0;
    char delimiter[1024]; // balancing stack
    int len=0;

    *exprlen=0;

    while (nextline(&len))
    {
        int i;
        for (i=0;i<len;i++,(*exprlen)++)
        {
            switch(expr[*exprlen])
            {
                case '\\': i++; (*exprlen)++; break; // don't interpret next char
                case '(': delimiter[++depth]=')'; break;
                case '[': delimiter[++depth]=']'; break;
                case '{': delimiter[++depth]='}'; break;
                case '<': delimiter[++depth]='>'; break;
                case ')': case ']': case '}': case '>':
                    if (depth)
                    {
                        if (expr[*exprlen]==delimiter[depth]) depth--;
                        else
                        {
                            printf("ERROR: Sequence unbalanced at \"%c\", offset %d\n",expr[*exprlen],*exprlen);
                            free(expr); expr=NULL;
                            *exprlen=depth=0;
                            goto done;
                        }
                    }
                    break;
                default: break;
            }
        }
        if (!depth)
            break;
    }

 done:
    return (*exprlen && !depth)?expr:(free(expr),NULL);
}


typedef enum {
    TOK_NONE     =0,

    // actionables
    TOK_FILE     =1<<0x00,
    TOK_EXPR     =1<<0x01,
    TOK_EXEC     =1<<0x02,
    TOK_SCOPE    =1<<0x03,
    TOK_CURLY    =1<<0x04,
    TOK_LIT      =1<<0x05,
    TOK_OP       =1<<0x06,
    TOK_REF      =1<<0x07,

    TOK_CONT     =1<<0x08,
    TOK_ADD      =1<<0x09,
    TOK_REM      =1<<0x0a,
    TOK_REV      =1<<0x0b,
    TOK_TYPES    = TOK_FILE | TOK_EXPR | TOK_EXEC | TOK_SCOPE | TOK_CURLY | TOK_LIT | TOK_OP | TOK_REF,
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
    CLL dict;  // cll of ltvr
    CLL anons; // cll of ltvr
    CLL toks;  // cll of tok
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

void *TOK_show(CLL *lnk,void *data) {
    TOK *tok=(TOK *) lnk;
    if (data) printf("%s",(char *) data);
    if (tok->flags&TOK_FILE)    printf("FILE "), fprintf(stdout,"0x%x",tok->data),putchar(' ');
    if (tok->flags&TOK_EXPR)    printf("EXPR "), fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_EXEC)    printf("EXEC " ),fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_SCOPE)   printf("SCOPE"), fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_CURLY)   printf("CURLY "),fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_LIT)     printf("LIT "),  fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_OP)      printf("OP "),   fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_REF)     printf("REF "),  fstrnprint(stdout,tok->data,tok->len),putchar(' ');
    if (tok->flags&TOK_ADD)     printf("ADD ");
    if (tok->flags&TOK_REM)     printf("REM ");
    if (tok->flags&TOK_REV)     printf("REV ");
    if (tok->flags&TOK_CONT)    printf("CONT ");
    if (tok->flags==TOK_NONE)   printf("_ ");
    else {
        printf("(");
        CLL_map(&tok->subtoks,FWD,TOK_show,NULL);
        printf(") ");
    }
    return NULL;
}

CONTEXT *CONTEXT_new(TOK *tok)
{
    CONTEXT *context=NULL;
    if (tok && (context=NEW(CONTEXT))) {
        CLL_init(&context->dict); CLL_init(&context->anons);
        CLL_put(CLL_init(&context->toks),&tok->cll,HEAD);
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

void *CONTEXT_show(CLL *lnk,void *data) {
    CONTEXT *context=(CONTEXT *) lnk;
    FILE *dumpfile=(FILE *) data;
    fprintf(dumpfile,"Context:\n");
    fprintf(dumpfile,"%1$d [label=\"anon\" color=blue] %1$d -> %2$d\n",&context->anons,context->anons.lnk[0]);
    fprintf(dumpfile,"%1$d [label=\"toks\" color=blue] %1$d -> %2$d\n",&context->toks,context->toks.lnk[0]);
    return NULL;
}



#define LTV_NIL (LTV_new("",0,LT_DUP))


int edict_eval(EDICT *edict)
{
    int status=0;
    char *bc_ws=CONCATA(bc_ws,edict->bc,WHITESPACE);

    int eval_context(CONTEXT *context) {
        int status=0;

        int eval_tok(TOK *tok) {
            int status=0;
            int add=0,reverse=0;
            char op;


#if 0
            if (expr->flags&TOK_SCOPE)
                STRY(!LTV_push(&edict->dict,(expr->context=LTV_pop(&edict->anon))),"pushing scope");
            else if (expr->flags&TOK_EXEC)
            {
                TOK *sub_expr=NULL;
                STRY(!(expr->context=LTV_pop(&edict->anon)),"popping lambda");
                STRY(!LTV_push(&edict->dict,LTV_NIL),"pushing null scope");
                STRY(!(sub_expr=TOK_new(TOK_EXPR,expr->context->data,expr->context->len)),"allocating function token");
                STRY(!CLL_splice(&expr->subtoks,&sub_expr->cll,TAIL),"appending function token");
            }

            // WRONG do not process recursively; push expr subtoks into edict->toks instead
            STRY(eval(&expr->subtoks),"traversing expr token");

            if (expr->flags&TOK_EXEC || expr->flags&TOK_SCOPE)
            { 
                LTV *ltv=NULL;
                STRY(!(ltv=LTV_pop(&edict->dict)),"popping scope");
                STRY((LTV_release(ltv),0),"releasing scope");
            }

            done:
            TOK_free(expr); // FIXME: skip this on error;
            return status;
        }

        int eval_atom(TOK *atom) {
            int status=0;
            TOK *op_subtok=NULL,*ref_subtok=NULL;
            TOK *op=NULL,*ref=NULL;

            void *eval_atom_subtok(CLL *cll,void *data) {
                TOK *atom_subtok=(TOK *) cll;
                int status=0;

                int eval_op()
                {
                    int status=0;
                    LTV *ltv=NULL;

                    // for each op, supply a lambda to a dict traverser that presents wildcard matches to the lambdas
                    switch (*op_subtok->data)
                    {
                    case '?':
                        edict_print(edict,(ref&&ref->lti)? ref->lti:NULL,-1);
                        break;
                    case '@':
                        if (ref&&ref->lti)
                            STRY(!LTV_put(&ref->lti->cll, (ltv=LTV_get(&edict->anon,1,0,NULL,0,NULL))?ltv:LTV_NIL, ((ref->flags&TOK_REVERSE)!=0), &ref->ltvr),
                                    "processing assignment op");
                        break;
                    case '/':
                        if (ref&&ref->ltvr)
                        {
                            LTVR_release(CLL_pop(&ref->ltvr->cll));
                            ref->ltvr=NULL; // seems heavy handed
                        }
                        else
                        {
                            STRY((LTV_release(LTV_pop(&edict->anon)),0),"releasing TOS");
                        }
                        break;
                    case '!':
                        if (ref)
                        {
                        }
                        else
                        {
                            if (CLL_EMPTY(&op_subtok->subtoks))
                            {
                                /// WRONG: rather than eval recursively, parse and insert new tokens onto token stack!!!
                                /// perhaps eval should just parse and splice
                                TOK *sub_expr=NULL;
                                STRY(!(op_subtok->context=LTV_pop(&edict->anon)),"popping anon function");
                                STRY(!(sub_expr=TOK_new(TOK_EXPR,op_subtok->context->data,op_subtok->context->len)),
                                        "allocating function token");
                                STRY(!CLL_splice(&op_subtok->subtoks,&sub_expr->cll,TAIL),"appending function token");
                            }
                            STRY(eval(&op_subtok->subtoks),"evaluating expr subtoks");
                        }
                        break;
                    default:
                        STRY(-1,"processing unimplemented OP %c",*op_subtok->data)
                        ;
                    }
                    done:return status;
                }

                int eval_ref(TOK *ref_subtok,TOK *op)
                {
                    TOK *parent=NULL;
                    int insert;

                    int resolve_ref(int insert)
                    {
                        void *lookup(CLL *cll,void *data)
                        {
                            LTVR *ltvr=(LTVR *) cll;
                            if (!ref_subtok->lti&&ltvr&&(ref_subtok->context=ltvr->ltv))
                                ref_subtok->lti=LT_find(&ltvr->ltv->rbr,ref_subtok->data,ref_subtok->len,insert);
                            return ref_subtok->lti? ref_subtok:NULL;
                        }

                        if (!parent) // possibly a leading ellipsis
                            ref=(ref_subtok->data==ELLIPSIS)?
                                    ref_subtok:lookup(CLL_get(&edict->dict,KEEP,HEAD),NULL);
                        else if (parent->data==ELLIPSIS) // trailing ellipsis
                            ref=CLL_map(&edict->dict,FWD,lookup,NULL);
                        else if (parent->ltvr)
                            ref=lookup(CLL_get(&parent->ltvr.cll,KEEP,HEAD),NULL);
                        if (ref&&ref->data==ELLIPSIS)
                            ;
                        return ref!=NULL;
                    }

                    void *eval_ref_lits(CLL *cll,void *data)
                    {
                        TOK *lit=(TOK *) cll;
                        int insertlit=(insert||(lit->flags&TOK_ADD));

                        if (!(lit->flags&TOK_LIT))
                            return printf("unexpected flag in REF\n"),atom_subtok;

                        if (resolve_ref(insertlit)&&ref->lti)
                            if (!LTV_get(&ref->lti->cll,0,(ref->flags&TOK_REVERSE)!=0,lit->data,lit->len,&ref->ltvr)
                                    &&insertlit)
                                LTV_put(&ref->lti->cll,LTV_new(lit->data,lit->len,LT_DUP|LT_ESC),
                                        (ref->flags&TOK_REVERSE)!=0,&ref->ltvr);

                        return NULL;
                    }

                    parent=ref;
                    ref=NULL;
                    insert=(atom_subtok->flags&TOK_ADD)||(op&&(op->flags&TOK_ADD));

                    // if inserting and parent wasn't found, add an empty value
                    if (parent&&!parent->ltvr&&insert)
                        LTV_put(&parent->lti->cll,LTV_NIL,(parent->flags&TOK_REVERSE)!=0,&parent->ltvr);

                    if (!CLL_EMPTY(&atom_subtok->subtoks)) // embedded lits
                        CLL_map(&atom_subtok->subtoks,FWD,eval_ref_lits,NULL);
                    else if (resolve_ref(insert)&&ref->lti) // no embedded lits
                        LTV_get(&ref->lti->cll,0,(ref->flags&TOK_REVERSE)!=0,NULL,0,&ref->ltvr);

                    done:return ref==NULL;
                }

                void *traverse_atom(CLL *cll,void *data)
                {
                    TOK *tok=(TOK *) cll;
                    void *traverse_refs(CLL *cll,void *data)
                    {
                        STRY(eval_ref(cll),"evaluating ref during sub-traverse");
                    }

                    parent=ref=NULL;
                    if (tok->flags=TOK_OP)
                        op=tok;
                    else if (!op)
                        STRY(((tok=(TOK *) CLL_map(&atom->subtoks,FWD,traverse_refs,NULL))!=NULL),
                                "sub-traversing atom");

                    if (op)
                        STRY(eval_op(),"evaluating op");
                    else if (ref && ref->ltvr)
                        STRY(!LTV_push(&edict->anon,ref->ltvr->ltv),"dereferencing ref");

                    done:return tok;
                }

                STRY((CLL_map(&atom->subtoks,FWD,traverse_atom,NULL)!=NULL),"evaluating atom subtok");

                done:return status;
            }

            // rather than traverse atoms, it's better to record the current position within the expression
            // and evaluate the expression stack's top expression at it's curpos at each iteration.
            STRY(CLL_map(&atom->subtoks,FWD,eval_atom_subtok,NULL)!=NULL,"traversing atom subtoks");

#endif


            int eval_op_ref(TOK *tok) {
                int status=0;
                int depth=0;
                int flags=0;
                LTI *lti=NULL;
                LTVR *ltvr=NULL;

                void *descend(CLL *lnk,void *data) {
                    TOK *curtok=(TOK *) lnk;

                    flags|=curtok->flags; // account for mods, at least
                    printf("eval_op_ref: ");

                    if (curtok->flags&TOK_OP)
                    {
                        TOK_show(lnk,data);
                    }
                    if (curtok->flags&TOK_REF)
                    {
                        TOK_show(lnk,data);
                    }
                    printf("\n");

                    if (CLL_EMPTY(&curtok->subtoks))
                    {
                        printf("eval_op_ref finish\n");
                    }
                    else
                    {
                        depth++;
                        CLL_map(&curtok->subtoks,FWD,descend,NULL);
                    }

                    if (depth==1 && curtok->flags&TOK_OP)
                        tokpush(&context->cll,(TOK *) CLL_cut(&curtok->cll));
                }

                descend(&tok->cll,NULL);
                TOK_free((TOK *) CLL_cut(&tok->cll));
                done:
                return status;
            }

            int eval_lit(TOK *lit) {
                int status=0;
                LTV *anon=NULL;
                STRY((anon=LTV_new(lit->data,lit->len,LT_DUP|LT_ESC))==NULL,"allocating anon lit");
                STRY(!LTV_put(&context->anons,anon,reverse,NULL),"pushing anon");
                TOK_free((TOK *) CLL_cut(&lit->cll));
                done:
                return status;
            }

            int eval_expr(TOK *expr) {
                int status=0;

                int parse(CLL *toks)
                {
                    int status=0;
                    TOK *curtok=NULL;
                    char *edata=NULL;
                    int elen=0;
                    int flags=TOK_NONE;

                    // sequence of include chars, then sequence of not-exclude chars, then terminate balanced sequence
                    int series(char *include,char *exclude,char balance) {
                        int i=0,depth=0;
                        int inclen=include?strlen(include):0;
                        int exclen=exclude?strlen(exclude):0;
                        if (include) for (;i<elen;i++) if (edata[i]=='\\') i++; else if (!memchr(include,edata[i],inclen)) break;
                        if (exclude) for (;i<elen;i++) if (edata[i]=='\\') i++; else if (memchr(exclude,edata[i],exclen)) break;
                        if (balance) for (;i<elen;i++) if (edata[i]=='\\') i++; else if (!(depth+=(edata[i]==edata[0])?1:(edata[i]==balance)?-1:0)) { i+=1; break; }
                        return i;
                    }

                    void advance(adv) {
                        adv=MIN(adv,elen);
                        edata+=adv;
                        elen-=adv;
                    }

                    void append(int reset,TOK *tok,int adv) {
                        tok->flags|=flags;
                        flags=TOK_NONE;
                        advance(adv);
                        if (curtok && !(curtok->flags&TOK_OP) && (tok->flags&TOK_OP))
                            curtok=NULL;
                        if (reset)
                            curtok=NULL;
                        curtok=(TOK *) CLL_splice(curtok?&curtok->subtoks:toks,&tok->cll,TAIL);
                        if (reset)
                            curtok=NULL;
                    }

                    if (expr && expr->data)
                    {
                        int tlen;
                        edata=expr->data;
                        elen=expr->len;

                        while (elen>0) {
                            switch(*edata) {
                            case ' ':
                            case '\t':
                            case '\n': tlen=series(WHITESPACE,NULL,0); advance(tlen); curtok=NULL; break;
                            case '<':  tlen=series(NULL,NULL,'>'); append(1,TOK_new(TOK_SCOPE,edata+1,tlen-2),tlen); break;
                            case '(':  tlen=series(NULL,NULL,')'); append(1,TOK_new(TOK_EXEC, edata+1,tlen-2),tlen); break;
                            case '{':  tlen=series(NULL,NULL,'}'); append(1,TOK_new(TOK_CURLY,edata+1,tlen-2),tlen); break;
                            case '[':  tlen=series(NULL,NULL,']'); append(0,TOK_new(TOK_LIT,  edata+1,tlen-2),tlen); break;
                            case '+':  flags|=TOK_ADD; advance(1); break;
                            case '-':  flags|=TOK_REV; advance(1); break;
                            case '@':
                            case '/':
                            case '!':
                            case '$':
                            case '&':
                            case '|':
                            case '?':  append(0,TOK_new(TOK_OP,edata,1),1); break;
                            case '.':  tlen=series(".",NULL,0);
                            switch(tlen) {
                            case 1:  advance(tlen); break;
                            case 2:  append(0,TOK_new(TOK_REF,ANONYMOUS,0),tlen); break;
                            default: append(0,TOK_new(TOK_REF,ELLIPSIS,3),tlen); break;
                            }
                            break;
                            default: tlen=series(NULL,bc_ws,0); if (tlen) append(0,TOK_new(TOK_REF,edata,tlen),tlen); break;
                            }
                        }
                    }

                    append(1,TOK_new(expr->flags|TOK_CONT,ANONYMOUS,0),0);

                    return elen;
                }

                if (expr->flags&TOK_CONT) // placeholder
                {
                    TOK_free((TOK *) CLL_cut(&expr->cll));
                    goto done;
                }

                STRY(parse(&expr->subtoks),"parse expr");
                CLL_MERGE(&context->toks,&expr->subtoks,HEAD);
                expr->flags|=TOK_CONT;

                done:
                return status;
            }


            int eval_file(TOK *file_tok) {
                int status=0;
                int len;
                char *line=NULL;
                TOK *expr=NULL;

                STRY(!file_tok->data,"validating file");
                TRY((line=edict_read((FILE *) file_tok->data,&len))==NULL,-1,read_failed,"reading from file");
                TRY((expr=TOK_new(TOK_EXPR,line,len))==NULL,-1,tok_failed,"allocating file-sourced expr token");
                tokpush(&context->toks,expr);
                goto done; // success!

                tok_failed:
                free(line);
                read_failed:
                fclose((FILE *) file_tok->data);
                TOK_free((TOK *) CLL_cut(&file_tok->cll));
                done:
                return status;
            }

            switch(tok->flags&TOK_TYPES)
            {
                case TOK_NONE:   TOK_free(tok); break;
                case TOK_LIT:    STRY(eval_lit(tok), "evaluating TOK_LIT expr");   break;
                case TOK_EXPR:   STRY(eval_expr(tok),"evaluating TOK_EXPR expr");  break;
                case TOK_EXEC:   STRY(eval_expr(tok),"evaluating TOK_EXEC expr");  break;
                case TOK_SCOPE:  STRY(eval_expr(tok),"evaluating TOK_SCOPE expr"); break;
                case TOK_CURLY:  STRY(eval_expr(tok),"evaluating TOK_CURLY expr"); break;
                case TOK_FILE:   STRY(eval_file(tok),"evaluating TOK_FILE expr");  break;
                case TOK_OP:     STRY(eval_op_ref(tok),"evaluating TOK_OP expr");  break;
                case TOK_REF:    STRY(eval_op_ref(tok),"evaluating TOK_REF expr"); break;
                default:         STRY(-1,"evaluating unexpected token expr");      break;
            }

            done:
            return status;
        }

        STRY(!context,"testing for null context");
        CLL_map(&context->toks,FWD,TOK_show,NULL), printf("\n");
        TRY((status=eval_tok((TOK *) CLL_get(&context->toks,KEEP,HEAD))),status,free_context,"running eval_tok");
        CLL_put(&edict->contexts,&context->cll,TAIL);
        goto done;
        free_context:
        CONTEXT_free(context);
        done:
        return status;
    }

    try_reset();
    while(!status) STRY(eval_context((CONTEXT *) CLL_get(&edict->contexts,FWD,POP)),"evaluating context");

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
 done:
    return status;
}
