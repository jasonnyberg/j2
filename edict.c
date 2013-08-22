
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

void *edict_traverse(CLL *cll,LTOBJ_OP preop,LTOBJ_OP postop)
{
    void *rval=NULL;
    struct LTOBJ_DATA ltobj_data = { preop,postop,0,NULL,0 };
    void *ltvr_op(CLL *cll,void *data) { return LTVR_traverse(cll,data); }
    rval=CLL_traverse(cll,FWD,ltvr_op,&ltobj_data);
    CLL_traverse(cll,FWD,ltvr_op,NULL); // cleanup "visited" flags
    return rval;
}

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
    TOK_CURLY    =1<<0x4,
    TOK_ATOM     =1<<0x5,
    TOK_LIT      =1<<0x6,
    TOK_OP       =1<<0x7,
    TOK_NAME     =1<<0x8,
    TOK_END      =1<<0x9,

    // modifiers
    TOK_ADD      =1<<0xa,
    TOK_REM      =1<<0xb,
    TOK_REVERSE  =1<<0xc,
    TOK_REGEXP   =1<<0xd,
    TOK_VIS      =1<<0xe,

    // masks
    TOK_TYPES     = TOK_FILE | TOK_EXPR | TOK_EXEC | TOK_SCOPE | TOK_CURLY | TOK_ATOM | TOK_LIT | TOK_OP | TOK_NAME,
    TOK_MODIFIERS = TOK_ADD | TOK_REM | TOK_REVERSE | TOK_REGEXP | TOK_VIS,
} TOK_FLAGS;


typedef struct 
{
    CLL cll;
    char *data;
    int len;
    TOK_FLAGS flags;
    CLL subtoks;
    LTI *lti;
    LTVR *ltvr; // points to LTV
    LTV *context; // for name or op nestings
} EDICT_TOK;

#define SUBTOK(lst,end) ((EDICT_TOK *) CLL_get((lst),KEEP,(end)))
#define DEQ(lst) ((EDICT_TOK *) CLL_get((lst),POP,HEAD))
#define ENQ(lst,tok) ((EDICT_TOK *) CLL_put(lst,&tok->cll,HEAD))
#define REQ(lst,tok) ((EDICT_TOK *) CLL_put(lst,&tok->cll,TAIL))

EDICT_TOK *TOK_new(TOK_FLAGS flags,char *data,int len)
{
    EDICT_TOK *tok=NULL;
    if ((flags || len) && (tok=(EDICT_TOK *) CLL_get(&tok_repo,POP,TAIL)) || (tok=NEW(EDICT_TOK)))
    {
        tok_count++;
        BZERO(*tok);
        tok->flags=flags;
        tok->data=data;
        tok->len=len;
        CLL_init(&tok->subtoks);
    }
    return tok;
}

void TOK_free(EDICT_TOK *tok)
{
    EDICT_TOK *subtok;
    
    while ((subtok=(EDICT_TOK *) CLL_get(&tok->subtoks,POP,HEAD)))
        TOK_free(subtok);

    BZERO(*tok);
    CLL_put(&tok_repo,&tok->cll,HEAD);
    tok_count--;
}

int edict_parse(EDICT *edict,EDICT_TOK *expr)
{
    int status=0;
    char *bc_ws=CONCATA(bc_ws,edict->bc,WHITESPACE);
    TOK_FLAGS flags=TOK_NONE;
    EDICT_TOK *atom=NULL,*name=NULL;
    char *edata=NULL;
    int elen=0;
    int tlen;

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

    EDICT_TOK *curatom(int reset) {
        name=NULL;
        if (reset) atom=NULL;
        return atom?atom:(atom=(EDICT_TOK *) CLL_put(&expr->subtoks,(CLL *) TOK_new(TOK_ATOM,"",0),TAIL));
    }

    EDICT_TOK *append(EDICT_TOK *parent,TOK_FLAGS flags,void *data,int len,int adv) {
        if (parent==NULL) parent=expr;
        if (parent==expr) atom=name=NULL;
        advance(adv);
        return (EDICT_TOK *) CLL_put(&parent->subtoks,(CLL *) TOK_new(flags,data,len),TAIL);
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
                case '@':
                    append(curatom(name!=NULL),TOK_OP|TOK_ADD,edata,1,1); // op after name resets atom
                    break;
                case '/':
                    append(curatom(name!=NULL),TOK_OP|TOK_REM,edata,1,1); // op after name resets atom
                    break;
                case '&':
                case '|':
                case '?':
                case '!':
                    append(curatom(name!=NULL),TOK_OP,edata,1,1); // op after name resets atom
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
    }

    return elen;
}


#define LTV_NIL (LTV_new("",0,LT_DUP))

int edict_repl(EDICT *edict)
{
    int status=0;
    CLL root;
    
    int eval(CLL *stack) {
        int status=0;
        EDICT_TOK *tok=NULL;
        
        void *show_tok(CLL *lnk,void *data) {
            EDICT_TOK *tok=(EDICT_TOK *) lnk;
            if (data) printf("%s",(char *) data);
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
            CLL_traverse(&tok->subtoks,FWD,show_tok,NULL);
            printf(") ");
            
            return NULL;
        }

        int eval_lit(EDICT_TOK *lit_tok) {
            // /* opt */ fstrnprint(stdout,tok->data,tok->len);
            return !LTV_put(&edict->anon,LTV_new(lit_tok->data,lit_tok->len,LT_DUP|LT_ESC),(lit_tok->flags&TOK_REVERSE)!=0,NULL);
        }
    
        int eval_expr(EDICT_TOK *expr) {
            int status=0;

            if (CLL_EMPTY(&expr->subtoks))
                edict_parse(edict,expr);

            CLL_traverse(&expr->subtoks,FWD,show_tok,NULL);
            printf("\n");

            if (expr->flags&TOK_SCOPE)
            {
                STRY(!LTV_push(&edict->dict,(expr->context=LTV_pop(&edict->anon))),"pushing scope");
            }
            else if (expr->flags&TOK_EXEC)
            {
                EDICT_TOK *sub_expr=NULL;
                STRY(!(expr->context=LTV_pop(&edict->anon)),"popping anon function");
                STRY(!LTV_push(&edict->dict,LTV_NIL),"pushing null scope");
                STRY(!(sub_expr=TOK_new(TOK_EXPR,expr->context->data,expr->context->len)),"allocating function token");
                STRY(!CLL_put(&expr->subtoks,&sub_expr->cll,TAIL),"appending function token");
            }
            
            STRY(eval(&expr->subtoks),"traversing expr token");
            
            if (expr->flags&TOK_EXEC || expr->flags&TOK_SCOPE)
            { 
                LTV *ltv=NULL;
                STRY(!(ltv=LTV_pop(&edict->dict)),"popping scope");
                STRY((LTV_release(ltv),0),"releasing scope");
            }
            
         done:
            return status;
        }

        int eval_atom(EDICT_TOK *atom) {            
            int status=0;
            EDICT_TOK *name_subtok=NULL;
            EDICT_TOK *op=NULL,*name=NULL;
            
            void *eval_atom_subtok(CLL *cll,void *data) {
                EDICT_TOK *atom_subtok=(EDICT_TOK *) cll;
                int status=0;
                
                int eval_name(EDICT_TOK *name_subtok) {
                    EDICT_TOK *parent=NULL;
                    int insert;
                    
                    int resolve_name(int insert) {
                        void *lookup(CLL *cll,void *data) {
                            LTVR *ltvr=(LTVR *) cll;
                            if (!name_subtok->lti && ltvr && (name_subtok->context=ltvr->ltv))
                                name_subtok->lti=LT_find(&ltvr->ltv->rbr,name_subtok->data,name_subtok->len,insert);
                            return name_subtok->lti?name_subtok:NULL;
                        }
                        
                        if (!parent) // possibly a leading ellipsis
                            name=(name_subtok->data==ELLIPSIS)?name_subtok:lookup(CLL_get(&edict->dict,KEEP,HEAD),NULL);
                        else if (parent->data==ELLIPSIS) // trailing ellipsis
                            name=CLL_traverse(&edict->dict,FWD,lookup,NULL);
                        else if (parent->ltvr)
                            name=lookup(CLL_get((CLL *) &parent->ltvr,KEEP,HEAD),NULL);
                        if (name && name->data==ELLIPSIS)
                            ;
                        return name!=NULL;
                    }
                    
                    void *eval_name_lits(CLL *cll,void *data) {
                        EDICT_TOK *lit=(EDICT_TOK *) cll;
                        int insertlit=(insert || lit->flags&TOK_ADD);
                        
                        if (!(lit->flags&TOK_LIT))
                            return printf("unexpected flag in NAME\n"),atom_subtok;
                        
                        if (resolve_name(insertlit) && name->lti)
                            if (!LTV_get(&name->lti->cll,0,(name->flags&TOK_REVERSE)!=0,lit->data,lit->len,&name->ltvr) && insertlit)
                                LTV_put(&name->lti->cll,LTV_new(lit->data,lit->len,LT_DUP|LT_ESC),(name->flags&TOK_REVERSE)!=0,&name->ltvr);
                        
                        return NULL;
                    }

                    if (name_subtok->flags&TOK_NAME)
                    parent=name;
                    name=NULL;
                    insert=(atom_subtok->flags&TOK_ADD) || (op && (op->flags&TOK_ADD));
                    
                    // if inserting and parent wasn't found, add an empty value
                    if (parent && !parent->ltvr && insert)
                        LTV_put(&parent->lti->cll,LTV_NIL,(parent->flags&TOK_REVERSE)!=0,&parent->ltvr);
                    
                    if (!CLL_EMPTY(&atom_subtok->subtoks)) // embedded lits
                        CLL_traverse(&atom_subtok->subtoks,FWD,eval_name_lits,NULL);
                    else if (resolve_name(insert) && name->lti) // no embedded lits
                        LTV_get(&name->lti->cll,0,(name->flags&TOK_REVERSE)!=0,NULL,0,&name->ltvr);
                    
                    return name==NULL;
                }

                int eval_op(EDICT_TOK *op_subtok) {
                    int status=0;
                    LTV *ltv=NULL;
                    op=op_subtok;

                    void *eval_names(CLL *cll,void *data) {
                        EDICT_TOK *tok=(EDICT_TOK *) cll;
                        return (tok->flags&TOK_NAME && eval_name(tok))?tok:NULL; // returns error tok or success
                    }
                    STRY((CLL_traverse(&atom->subtoks,FWD,eval_names,NULL)!=NULL),"pre-evaluating name toks");

                    // for each op, supply a lambda to a dict traverser that presents wildcard matches to the lambdas
                    switch(*op_subtok->data)
                    {
                        case '?':
                        {
                            edict_print(edict,(name && name->lti)?name->lti:NULL,-1);
                            break;
                        }
                        case '@':
                        {
                            if (name && name->lti)
                                STRY(!LTV_put(&name->lti->cll,
                                              (ltv=LTV_get(&edict->anon,1,0,NULL,0,NULL))?ltv:LTV_NIL,
                                              ((name->flags&TOK_REVERSE)!=0),
                                              &name->ltvr),"processing assignment op");
                            break;
                        }
                        case '/':
                        {
                            if (name && name->ltvr)
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
                                    EDICT_TOK *sub_expr=NULL;
                                    STRY(!(op_subtok->context=LTV_pop(&edict->anon)),"popping anon function");
                                    STRY(!(sub_expr=TOK_new(TOK_EXPR,op_subtok->context->data,op_subtok->context->len)),"allocating function token");
                                    STRY(!CLL_put(&op_subtok->subtoks,&sub_expr->cll,TAIL),"appending function token");
                                }
                                STRY(eval(&op_subtok->subtoks),"evaluating expr subtoks");
                            }
                            break;
                        }
                        default: STRY(-1,"processing unimplemented OP %c",*op_subtok->data);
                    }
                 done:
                    return status;
                }

                switch(atom_subtok->flags&TOK_TYPES)
                {
                    case TOK_OP:   STRY(eval_op(atom_subtok),"evaluating op"); break;
                    case TOK_NAME: if (!op) STRY(eval_name(atom_subtok),"evaluating name"); break;
                    default:       STRY(-1,"processing unexpected tok type in ATOM"); break;
                }
                
             done:
                return status?atom_subtok:NULL;
            }
            
            STRY(CLL_traverse(&atom->subtoks,FWD,eval_atom_subtok,NULL)!=NULL,"evaluating atom's subtoks");

            if (name && name->ltvr && !op) // atom is a simple name deref
                STRY(!LTV_push(&edict->anon,name->ltvr->ltv),"dereferencing name");

         done:
            return status;
        }
        
        int eval_file(EDICT_TOK *file) {
            int status=0;
            int len;
            char *line;
            FILE *ifile=NULL;
            
            STRY(!(ifile=file->data?fopen(file->data,"r"):stdin),"validating file opened properly");
            
            while (!status && (line=edict_read(ifile,&len)))
            {
                EDICT_TOK *expr=NULL;
                TRY(!ENQ(&file->subtoks,TOK_new(TOK_EXPR,line,len)),-1,free_line,"allocating file token");
                TRY((status=eval(&file->subtoks)),status,free_line,"processing file expr");
            free_line:
                free(line);
            }

            if (file->data && ifile) fclose(ifile);
         done:
            return 0;
        }
        
        while ((tok=DEQ(stack)))
        {
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
            TOK_free(tok);
        }
        
 done:
        if (tok)
        {
            if (status) show_tok(&tok->cll,"ERROR! token: ");
            TOK_free(tok);
        }
        return status;
    }

    try_reset();
    CLL_init(&root);
    while(1) {
        ENQ(&root,TOK_new(TOK_FILE,NULL,0)); // queue tok to read from stdin
        eval(&root);
    }
    
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

