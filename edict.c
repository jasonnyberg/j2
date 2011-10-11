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

int edict_delimit(char *str,int rlen,char *delim)
{
    return str && rlen && delim?strncspn(str,rlen,delim):0;
}

LTV *edict_get(EDICT *edict,char *name,int len,int pop,void **metadata,LTI **lti)
{
    void *internal_get(CLL *cll,void *data)
    {
        void *match=NULL;
        int nlen=0,mlen=0,alen=0,tlen=0,matchlen=0;
        int end=0,last=0;
        LTVR *ltvr=(LTVR *) cll; // each CLL node is really an LTVR
        LTV *root=ltvr->ltv,*newroot=NULL;
        
        if (len<0) len=strlen(name);
        if (len>0 && name[len]!='.') do
        {
            *metadata=NULL;
            match=NULL;
            matchlen=0;
            pop=pop && !(root->flags&LT_RO);
            nlen=edict_delimit(name,len,"."); // end of layer name
            mlen=edict_delimit(name,len,"="); // test specific value
            alen=edict_delimit(name,len,"+"); // test/add specific value
            tlen=MIN(mlen,alen);
            end=(*name=='-');
            last=(name[nlen]!='.');
            if (tlen<nlen)
            {
                match=name+tlen+1; // look for specific item rather than first or last
                matchlen=nlen-(tlen+1);
            }
            if (!((*lti)=LT_find(&root->rbr,name+end,MIN(tlen,nlen)-end,alen<len))) // if test/add, build name tree
                break;
            if (!(newroot=LTV_get(&(*lti)->cll,pop&&last,end,match,matchlen,metadata)) && alen<len)
            {
                newroot=LTV_new(match,matchlen,LT_DUP);
                if (!pop) LTV_put(&(*lti)->cll,newroot,end,0);
            }
            if (last)
            {
                if (pop && CLL_EMPTY(&(*lti)->cll))
                    RBN_release(&root->rbr,&(*lti)->rbn,LTI_release);
                return newroot;
            }
            root=newroot;
            len-=(nlen+1);
            name+=(nlen+1);
        } while(len>0 && root);
        pop=0; // only outermost namespace can delete
        return NULL;
    }

    LTV *result=name[0]=='.'?
        (name++,len--,internal_get(CLL_get(&edict->dict,0,0),NULL)): // first layer only
        CLL_traverse(&edict->dict,0,internal_get,NULL);
    return result?result:edict->nil;
}

LTV *edict_nameltv(EDICT *edict,char *name,int len,void *metadata,LTV *ltv)
{
    LTVR *ltvr=NULL;
    void *match=NULL;
    int nlen=0,mlen=0,alen=0,tlen=0,matchlen=0;
    LTI *lti=NULL;
    int end=0,last=0;
    LTV *root=LTV_get(&edict->dict,0,0,NULL,-1,&ltvr);

    if (len<0) len=strlen(name);
    if (len>0 && name[len]!='.') do
    {
        nlen=edict_delimit(name,len,"."); // end of layer name
        mlen=edict_delimit(name,len,"="); // test specific value
        alen=edict_delimit(name,len,"+"); // test/add specific value
        tlen=MIN(mlen,alen);
        end=(*name=='-');
        last=(name[nlen]!='.');
        if (tlen<nlen)
        {
            match=name+tlen+1; // look for specific item rather than first or last
            matchlen=nlen-(tlen+1);
        }
        if ((lti=LT_find(&root->rbr,name+end,MIN(tlen,nlen)-end,1)))
        {
            if (last)
                return LTV_put(&lti->cll,ltv?ltv:edict->nil,end,metadata);
            if (!(root=LTV_get(&lti->cll,0,end,match,matchlen,&md)))
                root=LTV_put(&lti->cll,LTV_new(match,matchlen,LT_DUP),end,NULL);
            len-=(nlen+1);
            name+=(nlen+1);
        }
    } while(len>0 && root);
    return NULL;
}

LTV *edict_add(EDICT *edict,LTV *ltv,void *metadata)
{
    return LTV_put(&edict->anon,ltv,0,NULL);
}

LTV *edict_rem(EDICT *edict,void **metadata)
{
    return LTV_get(&edict->anon,1,0,NULL,0,metadata); // cleanup: LTV_release(LTV *)
}

LTV *edict_name(EDICT *edict,char *name,int len,void *metadata)
{
    void *md;
    return edict_nameltv(edict,name,len,metadata,edict_rem(edict,&md));
}

int dwarf_import(EDICT *edict,char *module)
{
    char *cmd=FORMATA(cmd,,"../dwarf/myreader %s",module);
    //return edict_readfile(edict,popen(cmd,"r"));
}

LTV *edict_ref(EDICT *edict,char *name,int len,int pop,void *metadata)
{
    void *md;
    LTI *lti=NULL;
    LTV *ltv=edict_get(edict,name,len,pop,&md,&lti);

#ifndef DYNCALL // FIXME: tear this shit out when dyncall is integrated
    if (!strncmp(name,"dump",len))
    {
        LTV_release(ltv);
        edict_dump(edict);
    }
    else
    if (!strncmp(name,"import",len))
    {
        LTV *module=edict_rem(edict,&md);
        LTV_release(ltv);
        if (module && module!=edict->nil && module->len)
            dwarf_import(edict,module->data);
        LTV_release(module);
    }
    else
#endif
    return edict_add(edict,ltv?ltv:edict->nil,metadata);
}

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// PARSER
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

char *edict_read(FILE *ifile,int *exprlen)
{
    char *expr=NULL;

    char *nextline(int *len)
    {
        static char *line=NULL;
        static int buflen=0;

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
                case ')': case ']': case '}': case ">":
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
    TOK_FILE     =1<<0,
    TOK_EXPR     =1<<1,
    TOK_LIT      =1<<2,
    TOK_OP       =1<<3,
    TOK_NAME     =1<<4,
    TOK_ELLIPSIS =1<<5,

    // modifiers
    TOK_ADD      =1<<6,
    TOK_REVERSE  =1<<7,
    TOK_REGEXP   =1<<8,

    // etc.
    TOK_FREE     =1<<9,
    TOK_LEAF     =1<<10,

    // masks
    TOK_TYPES     = TOK_FILE | TOK_EXPR | TOK_LIT | TOK_NAME | TOK_OP | TOK_ELLIPSIS,
    TOK_MODIFIERS = TOK_ADD | TOK_REVERSE | TOK_REGEXP,
    TOK_ETC       = TOK_FREE | TOK_LEAF,
    
    // composites
    TOK_EXPR_FILE   = TOK_EXPR | TOK_FILE,
    TOK_NAME_REGEXP = TOK_NAME | TOK_REGEXP,
    TOK_REF         = TOK_NAME | TOK_ELLIPSIS,
    TOK_ATOM        = TOK_OP | TOK_REF | TOK_LIT,
} TOK_FLAGS;


typedef struct 
{
    CLL cll;
    void *data;
    int len;
    TOK_FLAGS flags;
    CLL items;
    LTI *lti;
    LTVR *ltvr;
    LTV *ltv;
} EDICT_TOK;

EDICT_TOK *TOK_new(TOK_FLAGS flags,void *data,int len)
{
    EDICT_TOK *tok=NULL;
    if ((flags || len) && (tok=(EDICT_TOK *) CLL_get(&tok_repo,1,1)) || (tok=NEW(EDICT_TOK)))
    {
        tok_count++;
        tok->flags=flags;
        tok->data=data;
        tok->len=len;
        CLL_init(&tok->items);
        lti=ltvr=ltv=NULL;
    }
    return tok;
}

void TOK_free(EDICT_TOK *tok)
{
    EDICT_TOK *subtok;
    
    while ((subtok=CLL_get(&tok->items,POP,HEAD)))
        TOK_free(subtok);

    if (tok->flags&TOK_FREE)
        free(tok->data);
    
    ZERO(*tok);
    CLL_put(&tok_repo,&tok->cll,0);
    tok_count--;
}

#define LABSTR(s,l,label...) { printf(label); fstrnprint(stdout,s,l); printf(" "); }


void edict_parse(EDICT *edict,EDICT_TOK *expr)
{
    char *ws=" \t\n";
    char *bc_ws=CONCATA(bc_ws,edict->bc,ws);
    int status=0;
    int tlen;
    TOK_FLAGS flags=TOK_NONE;
    EDICT_TOK *atom=NULL,*name=NULL;
    
    int series(char *including,char *excluding,char balance)
    {
        int i,offset,depth,found,end;
        int ilen=including?strlen(including):0;
        int elen=excluding?strlen(excluding):0;
        
        for (i=offset=depth=end=found=0;i<expr->len;i++)
        {
            if (!found && expr->data[i+offset]=='\\') expr->len--,offset++,expr->data[i]=expr->data[(i++)+offset];
            if (offset) expr->data[i]=expr->data[i+offset];

            if (!found)
            {
                if (balance)
                {
                    if (expr->data[i]==expr->data[0])
                        depth++;
                    else if (expr->data[i]==balance)
                        found=!(--depth);
                }
                else found=
                    (including && !memchr(including,expr->data[i],ilen)) ||
                    (excluding && memchr(excluding,expr->data[i],elen));
                
                end=i;
            }
            
            if (found && !offset)
                break;
        }
        
        return found?end:expr->len;
    }

    void advance(adv)
    {
        adv=MIN(adv,expr->len);
        expr->data+=adv;
        expr->len-=adv;
    }

    EDICT_TOK *tok_atom() { return atom?atom:(atom=CLL_put(&expr->items,TOK_new(TOK_ATOM,"",0),TAIL)); }

    EDICT_TOK *tok_append(EDICT_TOK *parent,TOK_FLAGS flags,void *data,int len,int adv)
    {
        EDICT_TOK *tok=TOK_new(flags,data,len);
        CLL_put(&parent->items,TOK_new(flags,data,len),TAIL);
        advance(adv);
        if (parent==expr)
            atom=name=NULL;
        else if (parent==atom)
            name=NULL;
        return tok;
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
                tlen=series(ws,NULL,0); // !!!
                tok_append(expr,TOK_NONE,expr->data,tlen,tlen);
                break;
            case '<': case '>': case '(': case ')': case '{': case '}':
                tok_append(expr,TOK_OP,expr->data,1,1);
                break;
            case '@': case '/': case '!': case '&': case '|': case '?':
                if (name) // following a name, an op terminates an atom
                    atom=name=NULL;
                tok_append(tok_atom(),TOK_OP,expr->data,1,1);
                break;
            case '[':
                tlen=series(NULL,NULL,']'); // !!!
                if (tok_name) tok_append(tok_name,TOK_LIT | flags,expr->data+1,tlen-1,tlen+1); // name lit
                else tok_append(expr,TOK_LIT | flags,expr->data+1,tlen-1,tlen+1); // naked lit
                break;
            case '.':
                tlen=series(".",NULL,0);
                if (tlen>2) // "..." terminates an atom
                    tok_append(tok_atom(),TOK_ELLIPSIS | flags,expr->data,0,3),atom=name=NULL;
                else if (tlen==2 | !name) // zero-len subname
                    tok_append=adv_tok(tok_atom(),TOK_NAME | flags,"",0,1);
                else // delimits a nonzero-len subname
                    advance(1),name=NULL;
                break;
            default:
                if ((tlen=series(NULL,bc_ws,0))) // !!!
                    name=adv_tok(tok_atom(),TOK_NAME | flags,expr->data,tlen,tlen);
                break;
        }

        flags=TOK_NONE;
    }
}


// if iter==true, return tok * if reprocessing of token is required
// else return tok * if there was an error processing the token
EDICT_TOK *edict_tok(EDICT *edict,EDICT_TOK *tok,int iter)
{
    int status=0;
    TOK_FLAGS types=tok->flags | TOK_TYPES;
    
    EDICT_TOK *edict_op_ref(EDICT_TOK *op_ref,int iter)
    {
        EDICT_TOK *resolved=NULL;
        LTVR *ltvr=NULL;
        LTV *ltv=NULL;

        void *resolve(CLL *lnk,void *data)
        {
            EDICT_TOK *tok=(EDICT_TOK *) lnk;
            TOK_FLAGS types=tok->flags & TOK_TYPES;
            TOK_FLAGS modifiers=tok->flags & TOK_MODIFIERS;

            else if (types==TOK_ELLIPSIS)
            {
                resolved=NULL;
            }
            else if (types==TOK_NAME_REGEXP)
            {
                resolved=NULL;
            }
            else if (types==TOK_NAME)
            {
                resolved=LT_find(resolved->,tok->data,tok->len,modifiers & TOK_ADD);
            }
            return NULL;
        }

        void *process(CLL *lnk,void *data)
        {
            EDICT_TOK *tok=(EDICT_TOK *) lnk;
            TOK_FLAGS types=tok->flags & TOK_TYPES;
            TOK_FLAGS modifiers=tok->flags & TOK_MODIFIERS;

            if (resolved) // or not resolved, whatever the op needs
            {
            }
            else return NULL;
        }

        void *iterate(CLL *lnk,void *data)
        {
            EDICT_TOK *tok=(EDICT_TOK *) lnk;
            TOK_FLAGS types=tok->flags & TOK_TYPES;
            TOK_FLAGS modifiers=tok->flags & TOK_MODIFIERS;

            else if (types==TOK_ELLIPSIS)
            {
            }
            else if (types==TOK_NAME_REGEXP)
            {
            }
            return NULL;
            
        }

        if (iter) tok=(EDICT_TOK *) CLL_traverse(op_ref->items,REV,iterate,NULL);
        else if (!CLL_traverse(op_ref->items,FWD,resolve,NULL))
            tok=CLL_traverse(op_ref->items,FWD,process,NULL);
        return tok;
    }

    EDICT_TOK *edict_expr(EDICT_TOK *expr,int iter)
    {
        EDICT_TOK *tok=NULL;
        
        void *process(CLL *lnk,void *data) { return edict_tok(edict,(EDICT_TOK *) lnk,0); }
        void *iterate(CLL *lnk,void *data) { return edict_tok(edict,(EDICT_TOK *) lnk,1); }
        
        if (CLL_EMPTY(&expr->items))
            edict_parse(edict,expr);

        if (iter) // single pass
            tok=(EDICT_TOK *) CLL_traverse(expr->items,REV,iterate,NULL);
        else
            while (!(tok=(EDICT_TOK *) CLL_traverse(expr->items,FWD,process,NULL)) &&
                   CLL_traverse(expr->items,REV,iterate,NULL));
        return tok;
    }

    EDICT_TOK *edict_expr_file(EDICT_TOK *tok,int iter)
    {
        int len;
        char *line;
        FILE *ifile=stdin;

        if (tok->data)
            ifile=fopen(tok->data,"r");
        
        while ((line=edict_read(ifile,&len)))
        {
            EDICT_TOK *expr=TOK_new(TOK_EXPR|TOK_FREE,line,len);
            EDICT_TOK *err_tok=edict_tok(edict,expr,0);
            // if (err_tok) token_error(expr,token);
            TOK_free(expr);
        }
        
        if (tok->data)
            fclose(ifile);
        
        return NULL;
    }

    if (tok->flags & TOK_REVERSE)    printf("(REV)");
    if (tok->flags & TOK_ADD)        printf("(ADD)");
    
    switch(action)
    {
        case TOK_NONE:               printf("\n"); break;
        case TOK_EXPR:               edict_expr(tok,0); break;
        case TOK_EXPR_FILE:          edict_expr_file(tok,0); break;
        case TOK_OP_REF:             edict_op_ref(tok,0); break;
        case TOK_LIT:                LABSTR(ltv->data,ltv->len,"LIT:"); break;
        case TOK_OP:                 LABSTR(ltv->data,ltv->len,"OP:"); break;
        case TOK_NAME:               LABSTR(ltv->data,ltv->len,"NAME:"); break;
        case TOK_ELLIPSIS:           printf("ELLIPSIS ");break;
    }

    return status;
}


int edict_repl(EDICT *edict)
{
    int status=0;
    EDICT_TOK *expr_file=TOK_new(TOK_FILE,NULL,0); // read from stdin
    status=edict_tok(tok);
    TOK_free(tok);
    return status;
}


/* NOTES:
 *
 * For regexp/traversing name context, store LTVR w/LTI as metadata to keep track of CLL's begin/end.
 *
 * Parse and create a CLL of tokens for each whitespace delineated expr,
 * that can be traversed fwd/back along with stacking/unstacking context. Metadata can store CLL of context?
 *
 * Parse tokens directly into CLL, and process @ whitespace. No intermediate storage.
 *
 *
 */


#if 0
int edict_repl(EDICT *edict)
{
    int status;
    void *offset;
    LTV *ltv;
    char *token;
    int skip;
    int len,ops;

    do
    {
        status=0;
        offset=0;
        code=NULL;
        token=NULL;
        len=ops=0;
        
 read:
        TRY(!((code=LTV_get(&edict->code,0,0,NULL,-1,&offset)) || // retrieve any stacked code first
              (code=LTV_put(&edict->code,edict_getline(edict,stdin),0,offset=0))),-1,done,"\n"); // otherwise, stdin

        if (code->flags&LT_FILE &&
            !(code=LTV_put(&edict->code,edict_getline(edict,code->data),0,offset=0))) // file ref, but no code
        {
            code=LTV_get(&edict->code,1,0,NULL,-1,&offset) // re-retrieve file ref
                fclose(code->data);
            LTV_release(code);
            goto read;
        }
        
        token=&code->data[offset];

        if (*token==0)
        {
            LTV_release(LTV_get(&edict->code,1,0,NULL,-1,&offset));
            goto read;
        }

        // start_expr();
        
        switch (*token)
        {
            int nest=0;

            // case '-': setreverse();
            // case '.': pushname(namelen);
            // case '+': set_condlit(); // conditional lit add
            // case '=': set_speclit(); // get specific lit
            // case '\'': pushslit(); // whitespace terminated
            // case '[': pushlit(); // balanced w/']'
            // case <ops>: pushops(); // use strspn
            // case <whitespace>: end_expr(); // clear expression stack
            // case <symchar>: namelen++;
            // case <wildcard>: setwild(),namelen++;
            
            case '\'':
                len=strcspn(token+1,edict->bcdel)+1;
                ops=1;
                break;
            case '[':
                len=edict_balance(token,"[]",&nest);
                ops=1;
                break;
            case '(':
            case ')':
            case '{':
            case '}':
            case '<':
            case '>':
                len=ops=1;
                break;
            default: // expression
                ops=minint(strcspn(token,"\'(){}[]<>"),strspn(token,edict->bc));
                len=ops+strcspn(token+ops,edict->bcdel);
                break;
        }
    
        offset+=len+strspn(token+len,WHITESPACE);

        if (offset < (void *) code->len)
            LTV_put(&edict->code,code,0,(void *) offset);

        if (len)
        {
            int i;
            if (ops)
                for (i=0;i<ops && i<len && edict->bcf[token[i]](edict,token+ops,len-ops);i++);
            else
                edict_ref(edict,token,len,0,NULL);
        }

        if (offset >= (void *) code->len)
            LTV_release(code);

        if (debug_dump)
            edict_dump(edict);
    } while (1);
    
 done:
    return status;
}
#endif


int bc_slit(EDICT *edict,char *name,int len)
{
    edict_add(edict,LTV_new(name,len,LT_DUP),NULL);
    return 0;
}

int bc_lit(EDICT *edict,char *name,int len)
{
    return bc_slit(edict,name,len-1);
}

int bc_subname(EDICT *edict,char *name,int len)
{
    return 1;
}

int bc_name(EDICT *edict,char *name,int len)
{
    edict_name(edict,name,len,NULL);
    return 1;
}

int bc_kill(EDICT *edict,char *name,int len)
{
    void *md;
    LTI *lti=NULL;
    LTV_release((name && len)?edict_get(edict,name,len,1,&md,&lti):edict_rem(edict,&md));
    return 1;
}

int bc_namespace_enter(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_put(&edict->dict,edict_rem(edict,&md),0,NULL);
    return 0;
}

int bc_namespace_leave(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_release(LTV_get(&edict->dict,1,0,NULL,-1,&md));
    return 0;
}

#if 0
int bc_exec_leave(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_put(&edict->code,LTV_get(&edict->dict,0,0,NULL,-1,&md),0,NULL);
    return bc_namespace_leave(edict,name,len);
}

int bc_map(EDICT *edict,char *name,int len)
{
    void *md;
    LTV *code=edict_rem(edict,&md);
    if (code && code!=edict->nil)
    {
        if (name && len) // iterate...
        {
            int uval;
            if (strtou(name,len,&uval)) // ...N times
            {
                if (uval)
                {
                    char *recurse=FORMATA(recurse,code->len+10+3,"[%s]!0x%x",code->data,uval-1);
                    LTV_put(&edict->code,LTV_new(recurse,-1,LT_DUP),0,NULL); // push code for tail recursion
                    LTV_put(&edict->code,code,0,NULL); // push code to execute
                }
            }
            else // ...through dict entry
            {
                LTI *lti=(void *) -1; // a NULL lti will indicate lookup fail
                LTV *data=edict_get(edict,name,len,1,&md,&lti);
                if (data && lti)
                {
                    char *recurse=FORMATA(recurse,code->len+len+3,"[%s]!%s",code->data,name);
                    LTV_put(&edict->code,LTV_new(recurse,code->len+len+3,LT_DUP),0,NULL); // push code for tail recursion
                    edict_add(edict,data,NULL); // push data from lookup
                    LTV_put(&edict->code,code,0,NULL); // push code to execute
                }
                LTV_release(data);
            }
        }
        else // single shot
            LTV_put(&edict->code,code,0,NULL); // push code to execute
    }
    LTV_release(code);
    return 0;
}


int bc_and(EDICT *edict,char *name,int len)
{
    void *md;
    LTV *code=edict_rem(edict,&md);
    if (code && code!=edict->nil)
    {
        if (name && len) // iterate through dict entry
        {
            LTI *lti=(void *) -1; // a NULL lti will indicate lookup fail
            LTV *data=edict_get(edict,name,len,0,&md,&lti);
            if (data && lti) // lookup succeeded
            {
                edict_add(edict,data,NULL); // push data from lookup
                LTV_put(&edict->code,code,0,NULL); // push code to execute
            }
            LTV_release(data);
        }
    }
    LTV_release(code);
    return 1;
}

int bc_or(EDICT *edict,char *name,int len)
{
    void *md;
    LTV *code=edict_rem(edict,&md);
    if (code && code!=edict->nil)
    {
        if (name && len) // iterate through dict entry
        {
            LTI *lti=(void *) -1; // a NULL lti will indicate lookup fail
            LTV *data=edict_get(edict,name,len,0,&md,&lti);
            if (data && !lti) // lookup failed
                LTV_put(&edict->code,code,0,NULL); // push code to execute
            LTV_release(data);
        }
    }
    LTV_release(code);
    return 1;
}

int bc_print(EDICT *edict,char *name,int len)
{
    edict_print(edict,name,len);
    return 1;
}
#endif

/*
  void *edict_pop(EDICT *edict,char *name,int len,int end
  LTV *edict_clone(LTV *ltv,int sibs)
  int edict_copy_item(EDICT *edict,LTV *ltv)
  int edict_copy(EDICT *edict,char *name,int len)
  int edict_raise(EDICT *edict,char *name,int len)
  void *edict_lookup(EDICT *edict,char *name,int len)
  void edict_display_item(LTV *ltv,char *prefix)
  void edict_list(EDICT *edict,char *buf,int len,int count,char *prefix)
  int edict_len(EDICT *edict,char *buf,int len)
  LTV *edict_getitem(EDICT *edict,char *name,int len,int pop)
  LTV *edict_getitems(EDICT *edict,LTV *repos,int display)
  LTV *edict_get_nth_item(EDICT *edict,int n)
*/

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
    
    edict_bytecode(edict,'[',bc_lit);
    edict_bytecode(edict,'.',bc_subname);
    edict_bytecode(edict,'@',bc_name);
    edict_bytecode(edict,'/',bc_kill);
    edict_bytecode(edict,'<',bc_namespace_enter);
    edict_bytecode(edict,'>',bc_namespace_leave);
    edict_bytecode(edict,'(',bc_namespace_enter); // exec_enter same as namespace_enter
    edict_bytecode(edict,')',bc_kill);
    edict_bytecode(edict,'{',bc_kill);
    edict_bytecode(edict,'}',bc_kill);
    edict_bytecode(edict,'+',bc_kill);
    edict_bytecode(edict,'-',bc_kill);
    edict_bytecode(edict,'!',bc_kill); // bc_map
    edict_bytecode(edict,'&',bc_kill); // bc_and
    edict_bytecode(edict,'|',bc_kill); // bc_or
    edict_bytecode(edict,'?',bc_kill); // bc_print
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
    LTV_put(&edict->dict,root,0,NULL);
    edict->nil=LTV_new("nil",3,LT_RO);
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

