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
    TOK_FILE     =1<<0x0,
    TOK_EXPR     =1<<0x1,
    TOK_EXEC     =1<<0x2,
    TOK_SCOPE    =1<<0x3,
    TOK_ITER     =1<<0x4,
    TOK_ATOM     =1<<0x5,
    TOK_LIT      =1<<0x6,
    TOK_OP       =1<<0x7,
    TOK_NAME     =1<<0x8,

    // modifiers
    TOK_ADD      =1<<0x9,
    TOK_REM      =1<<0xa,
    TOK_REVERSE  =1<<0xb,
    TOK_REGEXP   =1<<0xc,
    TOK_AVIS     =1<<0xd,
    TOK_RVIS     =1<<0xe,

    // masks
    TOK_TYPES     = TOK_FILE | TOK_EXPR | TOK_EXEC | TOK_SCOPE | TOK_ITER | TOK_ATOM | TOK_LIT | TOK_OP | TOK_NAME,
    TOK_MODIFIERS = TOK_ADD | TOK_REM | TOK_REVERSE | TOK_REGEXP | TOK_AVIS | TOK_RVIS,
} TOK_FLAGS;


typedef struct 
{
    CLL cll;
    char *data;
    int len;
    TOK_FLAGS flags;
    CLL items;
    LTV *parent_ltv;
    LTI *lti;
    LTVR *ltvr; // points to LTV
    LTV *context; // for nestings
} EDICT_TOK;

EDICT_TOK *TOK_new(TOK_FLAGS flags,char *data,int len)
{
    EDICT_TOK *tok=NULL;
    if ((flags || len) && (tok=(EDICT_TOK *) CLL_get(&tok_repo,1,1)) || (tok=NEW(EDICT_TOK)))
    {
        tok_count++;
        BZERO(*tok);
        tok->flags=flags;
        tok->data=data;
        tok->len=len;
        CLL_init(&tok->items);
    }
    return tok;
}

void TOK_free(EDICT_TOK *tok)
{
    EDICT_TOK *subtok;
    
    while ((subtok=(EDICT_TOK *) CLL_get(&tok->items,POP,HEAD)))
        TOK_free(subtok);

    BZERO(*tok);
    CLL_put(&tok_repo,&tok->cll,0);
    tok_count--;
}

int edict_parse(EDICT *edict,EDICT_TOK *expr)
{
    int status=0;
    char *ws=" \t\n";
    char *bc_ws=CONCATA(bc_ws,edict->bc,ws);
    int tlen;
    TOK_FLAGS flags=TOK_NONE;
    EDICT_TOK *atom=NULL,*name=NULL;

    // check for balance/matchset/notmatchset 
    int series(char *include,char *exclude,char balance) {
        int i,depth;
        int ilen=include?strlen(include):0;
        int elen=exclude?strlen(exclude):0;
        
        for (i=depth=0;i<expr->len;i++)
        {
            if (expr->data[i]=='\\') i++;
            else if (balance && !(depth+=(expr->data[i]==expr->data[0])?1:(expr->data[i]==balance)?-1:0)) return i+1;
            else if (include && !memchr(include,expr->data[i],ilen)) return i;
            else if (exclude && memchr(exclude,expr->data[i],elen)) return i;
        }
        
        return expr->len;
    }

    void advance(adv) {
        adv=MIN(adv,expr->len);
        expr->data+=adv;
        expr->len-=adv;
    }

    EDICT_TOK *curatom(int reset) {
        name=NULL;
        if (reset) atom=NULL;
        return atom?atom:(atom=(EDICT_TOK *) CLL_put(&expr->items,(CLL *) TOK_new(TOK_ATOM,"",0),TAIL));
    }

    EDICT_TOK *append(EDICT_TOK *parent,TOK_FLAGS flags,void *data,int len,int adv) {
        if (parent==NULL) parent=expr;
        if (parent==expr) atom=name=NULL;
        advance(adv);
        return (EDICT_TOK *) CLL_put(&parent->items,(CLL *) TOK_new(flags,data,len),TAIL);
    }
    
    while (expr->len>0)
    {
        switch(*expr->data)
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
                tlen=series(ws,NULL,0);
                append(expr,TOK_NONE,expr->data,tlen,tlen);
                break;
            case '(':
                tlen=series(NULL,NULL,')');
                append(expr,TOK_EXEC|flags,expr->data+1,tlen-2,tlen);
                break;
            case '{':
                tlen=series(NULL,NULL,'}');
                append(expr,TOK_ITER|flags,expr->data+1,tlen-2,tlen);
                break;
            case '<':
                tlen=series(NULL,NULL,'>');
                append(expr,TOK_SCOPE|flags,expr->data+1,tlen-2,tlen);
                break;
            case '[':
                tlen=series(NULL,NULL,']');
                append(name,TOK_LIT|flags,expr->data+1,tlen-2,tlen); // if name is null, reverts to expr
                break;
            case '@':
                append(curatom(name!=NULL),TOK_OP|TOK_ADD,expr->data,1,1); // op after name resets atom
                break;
            case '/':
                append(curatom(name!=NULL),TOK_OP|TOK_REM,expr->data,1,1); // op after name resets atom
                break;
            case '&': case '|': case '?':
                append(curatom(name!=NULL),TOK_OP,expr->data,1,1); // op after name resets atom
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
                    name=append(curatom(0),TOK_NAME | flags,expr->data,tlen,tlen);
                break;
        }

        flags=TOK_NONE;
    }

    return expr->len;
}


#define LTV_NIL (LTV_new("",0,LT_DUP))

int edict_repl(EDICT *edict)
{
    void *eval_expr(CLL *lnk,void *data) {
        EDICT_TOK *tok=(EDICT_TOK *) lnk;
        
        EDICT_TOK *none(EDICT_TOK *tok) {
            // /* opt */ fstrnprint(stdout,tok->data,tok->len);
            return NULL;
        }
        
        EDICT_TOK *lit(EDICT_TOK *tok) {
            // /* opt */ fstrnprint(stdout,tok->data,tok->len);
            return LTV_put(&edict->anon,LTV_new(tok->data,tok->len,LT_DUP|LT_ESC),(tok->flags&TOK_REVERSE)!=0,NULL)?NULL:tok;
        }
    
        EDICT_TOK *atom(EDICT_TOK *tok) {
            EDICT_TOK *errtok=NULL;
            EDICT_TOK *optok,*nametok;
            int req_op=0;

            void *eval_atom(CLL *lnk,void *data) {
                EDICT_TOK *errtok=NULL;
                EDICT_TOK *tok=(EDICT_TOK *) lnk;
                
                EDICT_TOK *name(EDICT_TOK *tok) {
                    EDICT_TOK *errtok=NULL;
                    EDICT_TOK *parenttok=nametok;
                    int insert=(tok->flags&TOK_ADD) || (optok && (optok->flags&TOK_ADD));
                    
                    EDICT_TOK *resolve_lti(int insert) {
                        EDICT_TOK *errtok=NULL;
                        void *lookup(CLL *cll,void *data) {
                            LTVR *ltvr=(LTVR *) cll;
                            if (!tok->lti && ltvr && (tok->parent_ltv=ltvr->ltv))
                                tok->lti=LT_find(&ltvr->ltv->rbr,tok->data,tok->len,insert);
                            return tok->lti?tok:NULL;
                        }
                        
                        if (!parenttok)
                            return (tok->data==ELLIPSIS)?tok:lookup(CLL_get(&edict->dict,0,0),NULL);
                        else if (parenttok->data==ELLIPSIS)
                            return CLL_traverse(&edict->dict,0,lookup,NULL);
                        else if (parenttok->ltvr)
                            return lookup(CLL_get((CLL *) &parenttok->ltvr,0,0),NULL);
                        else
                            return NULL;
                    }
                    
                    void *eval_namelits(CLL *cll,void *data) {
                        EDICT_TOK *lit=(EDICT_TOK *) cll;
                        EDICT_TOK *errtok=NULL;
                        int insertlit=(insert || lit->flags&TOK_ADD);
                        
                        if (!(lit->flags&TOK_LIT))
                            return printf("unexpected tok type in NAME\n"),tok;
                        
                        if ((nametok=resolve_lti(insertlit)) && nametok->lti)
                            if (!LTV_get(&nametok->lti->cll,0,(nametok->flags&TOK_REVERSE)!=0,lit->data,lit->len,&nametok->ltvr) && insertlit)
                                LTV_put(&nametok->lti->cll,LTV_new(lit->data,lit->len,LT_DUP|LT_ESC),(nametok->flags&TOK_REVERSE)!=0,&nametok->ltvr);
                        
                        return NULL;
                    }
                    
                    // if inserting and parent wasn't found, add an empty value
                    if (parenttok && !parenttok->ltvr && insert)
                        LTV_put(&parenttok->lti->cll,LTV_NIL,(parenttok->flags&TOK_REVERSE)!=0,&parenttok->ltvr);
                    
                    if (!CLL_EMPTY(&tok->items)) // embedded lits
                        errtok=(EDICT_TOK *) CLL_traverse(&tok->items,FWD,eval_namelits,NULL);
                    else if ((nametok=resolve_lti(insert)) && nametok->lti) // no embedded lits
                        LTV_get(&nametok->lti->cll,0,(nametok->flags&TOK_REVERSE)!=0,NULL,0,&nametok->ltvr);
                    return nametok;
                }
                
                switch(tok->flags&TOK_TYPES)
                {
                    case TOK_OP:
                        req_op=1; // atom has ops
                        if (!optok && tok->flags!=(tok->flags|=TOK_AVIS))
                            optok=tok;
                        break;
                    case TOK_NAME:
                        if ((!req_op || optok) && !(nametok=name(tok)))
                            errtok=tok;
                        break;
                    default:
                        printf("unexpected tok type in ATOM\n");
                        errtok=tok;
                        break;
                }
                return errtok;
            }

            LTV *ltv=NULL;
            while(optok=nametok=NULL,!(errtok=CLL_traverse(&tok->items,FWD,eval_atom,NULL)))
            {
                if (optok) switch(*optok->data)
                {
                    case '?':
                        edict_print(edict,NULL,0,-1);
                        break;
                    case '@':
                        if (nametok && nametok->lti)
                            LTV_put(&nametok->lti->cll,
                                    (ltv=LTV_get(&edict->anon,1,0,NULL,0,NULL))?ltv:LTV_NIL,
                                    ((nametok->flags&TOK_REVERSE)!=0),
                                    &nametok->ltvr);
                        break;
                    case '/':
                        if (nametok)
                        {
                            if (nametok->ltvr)
                            {
                                LTVR_release(CLL_pop(&nametok->ltvr->cll));
                                nametok->ltvr=NULL;
                            }
                            if (nametok->lti && CLL_EMPTY(&nametok->lti->cll))
                            {
                                RBN_release(&nametok->parent_ltv->rbr,&nametok->lti->rbn,LTI_release);
                                nametok->lti=NULL;
                            }
                        }
                        else
                        {
                            LTV_release(LTV_pop(&edict->anon));
                        }
                        break;
                    default: printf("OP %c not implemented\n",*optok->data); break;
                }
                else
                {
                    if (nametok && nametok->ltvr)
                        LTV_push(&edict->anon,nametok->ltvr->ltv);
                    break;
                }
            }
            return errtok;
        }
        
        EDICT_TOK *expr(EDICT_TOK *tok) {
            EDICT_TOK *errtok=NULL;

            EDICT_TOK *nest(int entering) {
                EDICT_TOK *errtok=NULL;
                if (tok->flags&TOK_SCOPE)
                    errtok=entering?
                        (LTV_push(&edict->dict,(tok->context=LTV_pop(&edict->anon)))?NULL:tok):
                        (LTV_release(LTV_pop(&edict->dict)),NULL);
                return errtok;
            }
            
            if (CLL_EMPTY(&tok->items))
            {
                edict_parse(edict,tok);
                if ((errtok=nest(1)) ||
                    (errtok=(EDICT_TOK *) CLL_traverse(&tok->items,FWD,eval_expr,NULL)) ||
                    (errtok=nest(0)))
                {
                    printf("Error: ");
                    fstrnprint(stdout,errtok->data,errtok->len);
                    printf("\n");
                }
            }
            return errtok;
        }

        EDICT_TOK *file(EDICT_TOK *tok) {
            int len;
            char *line;
            FILE *ifile=tok->data?fopen(tok->data,"r"):stdin;
            if (!ifile) return tok;
            while ((line=edict_read(ifile,&len)))
            {
                EDICT_TOK *expr_tok=TOK_new(TOK_EXPR,line,len);
                EDICT_TOK *errtok=expr(expr_tok);
                TOK_free(expr_tok);
                free(line);
            }
            if (tok->data) fclose(ifile);
            return NULL;
        }
    
        switch(tok->flags&TOK_TYPES)
        {
            case TOK_NONE:  return none(tok);
            case TOK_LIT:   return lit(tok); 
            case TOK_ATOM:  return atom(tok);
            case TOK_EXPR:  return expr(tok);
            case TOK_EXEC:  return expr(tok);
            case TOK_SCOPE: return expr(tok);
            case TOK_ITER:  return expr(tok);
            case TOK_FILE:  return file(tok);
            default: printf("unexpected tok type\n"); return tok;
        }
    }

    EDICT_TOK *tok=TOK_new(TOK_FILE,NULL,0); // read from stdin
    EDICT_TOK *errtok=eval_expr((CLL *) tok,NULL);
    TOK_free(tok);
    return errtok!=NULL;
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
    
    edict_bytecode(edict,'[',bc_dummy);
    edict_bytecode(edict,'.',bc_dummy);
    edict_bytecode(edict,'@',bc_dummy);
    edict_bytecode(edict,'/',bc_dummy);
    edict_bytecode(edict,'<',bc_dummy);
    edict_bytecode(edict,'>',bc_dummy);
    edict_bytecode(edict,'(',bc_dummy); // exec_enter same as namespace_enter
    edict_bytecode(edict,')',bc_dummy);
    edict_bytecode(edict,'{',bc_dummy);
    edict_bytecode(edict,'}',bc_dummy);
    edict_bytecode(edict,'+',bc_dummy);
    edict_bytecode(edict,'-',bc_dummy);
    edict_bytecode(edict,'!',bc_dummy); // bc_map
    edict_bytecode(edict,'&',bc_dummy); // bc_and
    edict_bytecode(edict,'|',bc_dummy); // bc_or
    edict_bytecode(edict,'?',bc_dummy); // bc_print
}

int edict_init(EDICT *edict,LTV *root)
{
    int status=0;
    BZERO(*edict);
    LT_init();
    TRY(!edict,-1,done,"\n");
    TRY(!(CLL_init(&edict->anon)),-1,done,"\n");
    TRY(!(CLL_init(&edict->dict)),-1,done,"\n");
    TRY(!(CLL_init(&edict->toks)),-1,done,"\n");
    CLL_init(&tok_repo);
    LTV_push(&edict->dict,root);
    edict_bytecodes(edict);
    //edict_readfile(edict,stdin);
    //edict_readfile(edict,fopen("/tmp/jj.in","r"));

 done:
    return status;
}

int edict_destroy(EDICT *edict)
{
    int status=0;
    TRY(!edict,-1,done,"\n");
    CLL_release(&edict->anon,LTVR_release);
    CLL_release(&edict->dict,LTVR_release);
    CLL_release(&edict->toks,LTVR_release);
 done:
    return status;
}

