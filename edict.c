
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
    TOK_CTXT     =1<<0x00,
    TOK_FILE     =1<<0x01,
    TOK_EXPR     =1<<0x02,
    TOK_EXEC     =1<<0x03,
    TOK_SCOPE    =1<<0x04,
    TOK_CURLY    =1<<0x05,
    TOK_ATOM     =1<<0x06,
    TOK_LIT      =1<<0x07,
    TOK_NAME     =1<<0x08,
    TOK_END      =1<<0x09,

    // modifiers
    TOK_ADD      =1<<0x0a,
    TOK_REM      =1<<0x0b,
    TOK_REVERSE  =1<<0x0c,
    TOK_REGEXP   =1<<0x0d,
    TOK_VIS      =1<<0x0e,

    // masks
    TOK_TYPES     = TOK_FILE | TOK_EXPR | TOK_EXEC | TOK_SCOPE | TOK_CURLY | TOK_ATOM | TOK_LIT | TOK_NAME,
    TOK_MODIFIERS = TOK_ADD | TOK_REM | TOK_REVERSE | TOK_REGEXP | TOK_VIS,
} TOK_FLAGS;


typedef struct NAME {
    struct NAME *next;
    LTI *lti;
    LTVR *ltvr; // points to LTV
} NAME;

typedef struct TOK {
    struct TOK *next;
    char *data;
    int len;
    char ops;
    TOK_FLAGS flags;
    struct TOK *subtoks;
    struct TOK *curtok; // use as tail ptr while constructing expr
    struct NAME *namestack;
} TOK;


typedef struct CONTEXT {
    struct CONTEXT *next;
    CLL anons;
    CLL exprs;
} CONTEXT;



/*
main {
    expr{
        // observe indexed op (add, rem, etc...)
        // if name, iterate name
        // pop/process op
        // push op on completed stack
    }

    while (1) {
        // select a context
        // eval token ops
        // pop/release token if exhausted
    }
}
*/

/* REDESIGN:
 * use 1d linked lists! (strict stack, push/pop)
 * put expressions on a stack in a context
 * keep track of an expression's next atom to process
 * after each atom gets processed, return and look for the next atom to schedule. (MULTITHREADING!)
 * one context per thread, which tracks: expression stack, anonymous values, context stack (dict namespace/function nesting)
 * each dict entry should track owner/occupier id
 * threads can't modify dict items that it doesn't own AND occupy. (can't add OR delete)
 * atoms keep track of their namestacks.
 */

TOK *tokpush(TOK *lst,TOK *tok) { return STACK_PUSH(lst,tok); }
TOK *tokpop(TOK *lst) { TOK *iter,*item=STACK_NEWITER(iter,lst); if (item) STACK_POP(iter); return item; }

TOK *TOK_new(TOK_FLAGS flags,char *data,int len,int ops)
{
    TOK *tok=NULL;
    if ((flags || len) && (tok=DEQ(&tok_repo)) || (tok=NEW(TOK)))
    {
        tok_count++;
        tok->flags=flags;
        tok->data=data;
        tok->len=len;
        tok->ops=ops; // reach backwards from data "ops" bytes...
    }
    return tok;
}

void TOK_free(TOK *tok)
{
    TOK *item,*iter;
    
    while ((item=STACK_NEWITER(iter,&tok->subtoks)))
        STACK_POP(iter),TOK_free(item);

    BZERO(*tok);
    tokpush(&tok_repo,tok);
    tok_count--;
}

int edict_parse(EDICT *edict,TOK *expr)
{
    int status=0;
    char *bc_ws=CONCATA(bc_ws,edict->bc,WHITESPACE);
    TOK_FLAGS flags=TOK_NONE;
    TOK *atom=NULL,*name=NULL;
    char *edata=NULL;
    int elen=0;
    int tlen;
    int ops=0;

    // check for balance/matchset/notmatchset 
    int series(char *include,char *exclude,char balance) {
        int i,depth;
        int inclen=include?strlen(include):0;
        int exclen=exclude?strlen(exclude):0;
        
        for (i=depth=0;i<elen;i++)
        {
            if (edata[i]=='\\') i++;
            else if (balance && !(depth+=(edata[i]==edata[0])?1:(edata[i]==balance)?-1:0)) return i+1;
            else if (include && !memchr(include,edata[i],inclen)) return i;
            else if (exclude && memchr(exclude,edata[i],exclen)) return i;
        }
        
        return elen;
    }

    void advance(adv) {
        adv=MIN(adv,elen);
        edata+=adv;
        elen-=adv;
    }

    TOK *curatom(int reset) {
        name=NULL;
        if (reset) atom=NULL;
        return atom?atom:(atom=(TOK *) CLL_sumi(&expr->subtoks,(CLL *) TOK_new(TOK_ATOM,"",0),TAIL));
    }

    TOK *append(TOK *parent,TOK_FLAGS flags,char *data,int len,int adv) {
        int olen=ops;
        ops=0;
        if (parent==NULL) parent=expr;
        if (parent==expr) atom=name=NULL;
        advance(adv);
        return (TOK *) CLL_sumi(&parent->subtoks,(CLL *) TOK_new(flags,data,len,ops),TAIL);
        return tok;
    }

    if (expr && expr->data)
    {
        edata=expr->data;
        elen=expr->len;
        
        while (elen>0)
        {
            switch(*edata)
            {
                case '-':
                    flags|=TOK_REVERSE;
                    advance(1);
                    continue; // !!!
                case '+':
                    flags|=TOK_ADD;
                    advance(1);
                    continue; // !!!
                case ' ': case '\t': case '\n':
                    tlen=series(WHITESPACE,NULL,0);
                    append(expr,TOK_NONE,edata,tlen,tlen);
                    break;
                case '<':
                    tlen=series(NULL,NULL,'>');
                    append(expr,TOK_SCOPE|flags,edata+1,tlen-2,tlen);
                    break;
                case '(':
                    tlen=series(NULL,NULL,')');
                    append(expr,TOK_EXEC|flags,edata+1,tlen-2,tlen);
                    break;
                case '{':
                    tlen=series(NULL,NULL,'}');
                    append(expr,TOK_CURLY|flags,edata+1,tlen-2,tlen);
                    break;
                case '[':
                    tlen=series(NULL,NULL,']');
                    append(name,TOK_LIT|flags,edata+1,tlen-2,tlen); // if name is null, reverts to expr
                    break;
                case '-': // cll reverse
                case '+': // add (vs. search)
                case '@': // push @name
                case '/': // pop @name
                case '$': // substitute???
                case '&': // logical and
                case '|': // logical or
                case '?': // print value?
                    ops++;
                    advance(1);
                    break;
                case '.':
                    tlen=series(".",NULL,0);
                    if (tlen>2)
                        append(curatom(0),TOK_NAME | flags,ELLIPSIS,3,3);
                    else if (tlen==2 | !name) // zero-len subname
                        name=append(curatom(0),TOK_NAME | flags,ANONYMOUS,0,1);
                    else // delimits a nonzero-len subname
                        advance(1),name=NULL;
                    break;
                default:
                    if ((tlen=series(NULL,bc_ws,0)))
                        name=append(curatom(0),TOK_NAME | flags,edata,tlen,tlen);
                    break;
            }

            flags=TOK_NONE;
        }
        if (ops)
            printf(stdout,CODE_RED,"Unassociated ops: "),fstrnprint(stdout,edata-ops,ops),printf("\n" CODE RESET);
    }

    return elen;
}


#define LTV_NIL (LTV_new("",0,LT_DUP))

int edict_repl(EDICT *edict)
{
    int status=0;

    int eval(TOK *toklist) {
        TOK *tok=tokpop(toklist);
        int status=0;
        
        int eval_lit(TOK *lit_tok) {
            int status=0;
            LTV *anon=NULL;

            STRY((anon=LTV_new(lit_tok->data,lit_tok->len,LT_DUP|LT_ESC))==NULL,"allocating anon lit");
            STRY(!LTV_put(&edict->anon,anon,(lit_tok->flags&TOK_REVERSE)!=0,NULL),"pushing anon");
            TOK_free(lit_tok);
         done:
            return status;
        }

    #if 0
        int eval_expr(TOK *expr) {
            int status=0;

            if (CLL_EMPTY(&expr->subtoks))
                edict_parse(edict,expr);

            if (expr->flags&TOK_SCOPE)
                STRY(!LTV_push(&edict->dict,(expr->context=LTV_pop(&edict->anon))),"pushing scope");
            else if (expr->flags&TOK_EXEC)
            {
                TOK *sub_expr=NULL;
                STRY(!(expr->context=LTV_pop(&edict->anon)),"popping anon function");
                STRY(!LTV_push(&edict->dict,LTV_NIL),"pushing null scope");
                STRY(!(sub_expr=TOK_new(TOK_EXPR,expr->context->data,expr->context->len)),"allocating function token");
                STRY(!CLL_sumi(&expr->subtoks,&sub_expr->cll,TAIL),"appending function token");
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
            CLL ops;
            TOK *op_subtok=NULL,*name_subtok=NULL;
            TOK *op=NULL,*name=NULL;

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
                    {
                        edict_print(edict,(name&&name->lti)? name->lti:NULL,-1);
                        break;
                    }
                    case '@':
                    {
                        if (name&&name->lti)
                            STRY(!LTV_put(&name->lti->cll, (ltv=LTV_get(&edict->anon,1,0,NULL,0,NULL))?ltv:LTV_NIL, ((name->flags&TOK_REVERSE)!=0), &name->ltvr),
                                 "processing assignment op");
                        break;
                    }
                    case '/':
                    {
                        if (name&&name->ltvr)
                        {
                            LTVR_release(CLL_pop(&name->ltvr->cll));
                            name->ltvr=NULL; // seems heavy handed
                        }
                        else
                        {
                            STRY((LTV_release(LTV_pop(&edict->anon)),0),"releasing TOS");
                        }
                        break;
                    }
                    case '!':
                    {
                        if (name)
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
                                STRY(!CLL_sumi(&op_subtok->subtoks,&sub_expr->cll,TAIL),"appending function token");
                            }
                            STRY(eval(&op_subtok->subtoks),"evaluating expr subtoks");
                        }
                        break;
                    }
                    default:
                        STRY(-1,"processing unimplemented OP %c",*op_subtok->data)
                        ;
                    }
                    done:return status;
                }

                int eval_name(TOK *name_subtok,TOK *op)
                {
                    TOK *parent=NULL;
                    int insert;

                    int resolve_name(int insert)
                    {
                        void *lookup(CLL *cll,void *data)
                        {
                            LTVR *ltvr=(LTVR *) cll;
                            if (!name_subtok->lti&&ltvr&&(name_subtok->context=ltvr->ltv))
                                name_subtok->lti=LT_find(&ltvr->ltv->rbr,name_subtok->data,name_subtok->len,insert);
                            return name_subtok->lti? name_subtok:NULL;
                        }

                        if (!parent) // possibly a leading ellipsis
                            name=(name_subtok->data==ELLIPSIS)?
                                    name_subtok:lookup(CLL_get(&edict->dict,KEEP,HEAD),NULL);
                        else if (parent->data==ELLIPSIS) // trailing ellipsis
                            name=CLL_map(&edict->dict,FWD,lookup,NULL);
                        else if (parent->ltvr)
                            name=lookup(CLL_get((CLL *) &parent->ltvr,KEEP,HEAD),NULL);
                        if (name&&name->data==ELLIPSIS)
                            ;
                        return name!=NULL;
                    }

                    void *eval_name_lits(CLL *cll,void *data)
                    {
                        TOK *lit=(TOK *) cll;
                        int insertlit=(insert||(lit->flags&TOK_ADD));

                        if (!(lit->flags&TOK_LIT))
                            return printf("unexpected flag in NAME\n"),atom_subtok;

                        if (resolve_name(insertlit)&&name->lti)
                            if (!LTV_get(&name->lti->cll,0,(name->flags&TOK_REVERSE)!=0,lit->data,lit->len,&name->ltvr)
                                    &&insertlit)
                                LTV_put(&name->lti->cll,LTV_new(lit->data,lit->len,LT_DUP|LT_ESC),
                                        (name->flags&TOK_REVERSE)!=0,&name->ltvr);

                        return NULL;
                    }

                    parent=name;
                    name=NULL;
                    insert=(atom_subtok->flags&TOK_ADD)||(op&&(op->flags&TOK_ADD));

                    // if inserting and parent wasn't found, add an empty value
                    if (parent&&!parent->ltvr&&insert)
                        LTV_put(&parent->lti->cll,LTV_NIL,(parent->flags&TOK_REVERSE)!=0,&parent->ltvr);

                    if (!CLL_EMPTY(&atom_subtok->subtoks)) // embedded lits
                        CLL_map(&atom_subtok->subtoks,FWD,eval_name_lits,NULL);
                    else if (resolve_name(insert)&&name->lti) // no embedded lits
                        LTV_get(&name->lti->cll,0,(name->flags&TOK_REVERSE)!=0,NULL,0,&name->ltvr);

                    done:return name==NULL;
                }

                void *traverse_atom(CLL *cll,void *data)
                {
                    TOK *tok=(TOK *) cll;
                    void *traverse_names(CLL *cll,void *data)
                    {
                        STRY(eval_name(cll),"evaluating name during sub-traverse");
                    }

                    parent=name=NULL;
                    if (tok->flags=TOK_OP)
                        op=tok;
                    else if (!op)
                        STRY(((tok=(TOK *) CLL_map(&atom->subtoks,FWD,traverse_names,NULL))!=NULL),
                                "sub-traversing atom");

                    if (op)
                        STRY(eval_op(),"evaluating op");
                    else if (name && name->ltvr)
                    STRY(!LTV_push(&edict->anon,name->ltvr->ltv),"dereferencing name");

                    done:return tok;
                }

                STRY((CLL_map(&atom->subtoks,FWD,traverse_atom,NULL)!=NULL),"evaluating atom subtok");

                done:return status;
            }
            
            // rather than traverse atoms, it's better to record the current position within the expression
            // and evaluate the expression stack's top expression at it's curpos at each iteration.
            STRY(CLL_map(&atom->subtoks,FWD,eval_atom_subtok,NULL)!=NULL,"traversing atom subtoks");

         done:
            TOK_free(atom); // FIXME: skip this on error;
            return status;
        }
#endif
        
        int eval_file(TOK *file_tok) {
            int status=0;
            int len;
            char *line=NULL;
            TOK *expr=NULL;
            
            STRY(!file_tok->data,"validating file");
            TRY((line=edict_read((FILE *) file_tok->data,&len))==NULL,-1,read_failed,"reading from file");
            TRY((expr=TOK_new(TOK_EXPR,line,len))==NULL,-1,tok_failed,"allocating file-sourced expr token");
            
            tokpush(&edict->toks,file);
            toppush(&edict->toks,expr);
            goto done; // success!
            
        tok_failed:
        tokpop(toklist);
            free(line);
        read_failed:
            fclose((FILE *) file->data);
            TOK_free(file_tok);
        done:
            return status;
        }
        
        switch(tok->flags&TOK_TYPES)
        {
            case TOK_NONE:   break;
            case TOK_LIT:    STRY(eval_lit(tok), "evaluating TOK_LIT expr");    break;
            case TOK_ATOM:   STRY(eval_atom(tok),"evaluating TOK_ATOM expr");   break;
            case TOK_EXPR:   STRY(eval_expr(tok),"evaluating TOK_EXPR expr");   break;
            case TOK_EXEC:   STRY(eval_expr(tok),"evaluating TOK_EXEC expr");   break;
            case TOK_SCOPE:  STRY(eval_expr(tok),"evaluating TOK_SCOPE expr");  break;
            case TOK_CURLY:  STRY(eval_expr(tok),"evaluating TOK_CURLY expr");  break;
            case TOK_FILE:   STRY(eval_file(tok),"evaluating TOK_FILE expr");   break;
            case TOK_OP:
            case TOK_NAME:
            default:         STRY(-1,"evaluating unexpected token expr");       break;
        }
        
     done:
        return status;
    }

    void *show_tok(CLL *lnk,void *data) {
        TOK *tok=(TOK *) lnk;
        if (data) printf("%s",(char *) data);
        if (ops) fstrnprintf(stdout,tok->data-tok->ops,tok->ops),printf(":");
        if (tok->flags&TOK_FILE)    printf("FILE "),fstrnprint(stdout,tok->data,tok->len),putchar(' ');
        if (tok->flags&TOK_EXPR)    printf("EXPR ");
        if (tok->flags&TOK_EXEC)    printf("EXEC " );
        if (tok->flags&TOK_SCOPE)   printf("SCOPE");
        if (tok->flags&TOK_CURLY)   printf("CURLY ");
        if (tok->flags&TOK_ATOM)    printf("ATOM ");
        if (tok->flags&TOK_LIT)     printf("LIT "),fstrnprint(stdout,tok->data,tok->len),putchar(' ');
        if (tok->flags&TOK_OP)      printf("OP "),fstrnprint(stdout,tok->data,tok->len),putchar(' ');
        if (tok->flags&TOK_NAME)    printf("NAME "),fstrnprint(stdout,tok->data,tok->len),putchar(' ');
        if (tok->flags&TOK_ADD)     printf("ADD ");
        if (tok->flags&TOK_REM)     printf("REM ");
        if (tok->flags&TOK_REVERSE) printf("REVERSE ");
        if (tok->flags&TOK_REGEXP)  printf("REGEXP ");
        if (tok->flags&TOK_VIS)     printf("VIS ");
        if (tok->flags==TOK_NONE)   printf("NONE ");
        printf("(");
        CLL_map(&tok->subtoks,FWD,show_tok,NULL);
        printf(") ");
        
        return NULL;
    }

    try_reset();
    while(tokpush(&edict->toks,TOK_new(TOK_FILE,stdin,sizeof(FILE *))))
    {
        tok *item,*iter;
        do CLL_map(&edict->toks,FWD,show_tok,NULL), printf("\n");
        while (eval(edict->toks));
    
 done:
    return status;
}




///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// Initialization
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////


int bc_dummy(EDICT *edict,char *name,int len)
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
    STRY(!CLL_init(&edict->anon),"initializing edict->anon");
    STRY(!CLL_init(&edict->dict),"initializing edict->dict");
    STRY(!CLL_init(&edict->toks),"initializing edict->toks");
    STRY(!CLL_init(&tok_repo),"initializing tok_repo");
    STRY(!LTV_push(&edict->dict,root),"pushing edict->dict root");
    STRY(edict_bytecodes(edict),"initializing bytecodes");

 done:
    return status;
}

int edict_destroy(EDICT *edict)
{
    int status=0;
    STRY(!edict,"validating arg edict");
    CLL_release(&edict->anon,LTVR_release);
    CLL_release(&edict->dict,LTVR_release);
    CLL_release(&edict->toks,LTVR_release);
 done:
    return status;
}

