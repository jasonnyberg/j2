#define _GNU_SOURCE
#include <stdlib.h>
#include "util.h"
#include "edict.h"


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

#define EDICT_NAMESEP ".,"

int edict_delimit(char *str,int rlen)
{
    int len = (str && *str)?strcspn(str,EDICT_NAMESEP):0;
    return rlen<len?rlen:len;
}

LTV *edict_nameltv(EDICT *edict,char *name,int len,int end,void *metadata,LTV *ltv)
{
    void *md;
    int tlen;
    LTI *lti;
    LTV *root=LTV_get(&edict->dict,0,0,&md);
    
    if (len<0) len=strlen(name);
    do
    {
        tlen=edict_delimit(name,len);
        if ((lti=LT_lookup(&root->rbr,name,tlen,1)))
        {
            if (name[tlen]=='.')
                root=LTV_get(&lti->cll,0,0,&md);
            else if (name[tlen]==',')
                root=LTV_get(&lti->cll,0,1,&md);
            else
                return LTV_put(&lti->cll,ltv?ltv:edict->nil,end,metadata);
            
            if (!root)
                root=LTV_put(&lti->cll,LTV_new("",-1,0),0,NULL);
            len-=(tlen+1);
            name+=(tlen+1);
        }
    } while(len>0 && root);
    return NULL;
}

LTV *edict_add(EDICT *edict,LTV *ltv,void *metadata)
{
    //return LTV_put(&edict->anon,ltv,0,metadata);
    return edict_nameltv(edict,"",0,0,metadata,ltv);
}

LTV *edict_rem(EDICT *edict,void **metadata)
{
    //return LTV_get(&edict->anon,1,0,metadata); // cleanup: LTV_release(LTV *)
    return edict_get(edict,"",0,1,0,metadata); // cleanup: LTV_release(LTV *)
}

LTV *edict_name(EDICT *edict,char *name,int len,int end,void *metadata)
{
    void *md;
    return edict_nameltv(edict,name,len,end,metadata,edict_rem(edict,&md));
}

LTV *edict_get(EDICT *edict,char *name,int len,int pop,int end,void **metadata)
{
    void *md;
    LTV *root=LTV_get(&edict->dict,0,0,&md);
    if (len<0) len=strlen(name);
    do
    {
        LTI *lti;
        int tlen=edict_delimit(name,len);
        if ((lti=LT_lookup(&root->rbr,name,tlen,0)))
        {
            pop=pop && !(root->flags&LT_RO);
            if (name[tlen]=='.')
                root=LTV_get(&lti->cll,0,0,&md);
            else if (name[tlen]==',')
                root=LTV_get(&lti->cll,0,1,&md);
            else
            {
                LTV *rval=LTV_get(&lti->cll,pop,end,metadata);
                if (pop && CLL_EMPTY(&lti->cll))
                    RBN_release(&root->rbr,&lti->rbn,LTI_release);
                return rval;
            }
            len-=(tlen+1);
            name+=(tlen+1);
        }
        else return edict->nil;
    } while(len>0 && root);
    
    return NULL;
}

LTV *edict_ref(EDICT *edict,char *name,int len,int pop,int end,void *metadata)
{
    void *md;
    LTV *ltv=edict_get(edict,name,len,pop,end,&md);
    return edict_add(edict,ltv?ltv:edict->nil,metadata);
}

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// PARSER
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

int edict_balance(char *str,char *start_end,int *nest)
{
    int i;
    for(i=0;str&&str[i];i++)
        if (str[i]==start_end[0]) (*nest)++;
        else if (str[i]==start_end[1] && --(*nest)==0)
            return ++i; // balanced
    return i; // unbalanced
}

 
LTV *edict_getline(EDICT *edict,FILE *stream)
{
    char *rbuf=NULL;
    char *buf=NULL;
    size_t bufsz;
    ssize_t len=0,totlen=0;
    int nest=1;
    
    printf("jj> ");
    while ((len=getline(&buf,&bufsz,stream))>0)
    {
        rbuf=RENEW(rbuf,totlen+len+1);
        strcpy(rbuf+totlen,buf);
        free(buf); // allocated via getline, so don't DELETE
        buf=NULL;
        edict_balance(rbuf+totlen,"[]",&nest);
        totlen+=len;
        if (nest==1)
            break;
        else printf("... ");
    }
    
    return totlen?LTV_new(rbuf,totlen,LT_DEL):NULL;
}


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
        ltv=NULL;
        token=NULL;
        len=ops=0;

        edict_dump(edict);
        
 read:
        if (!(ltv=LTV_get(&edict->code,1,0,&offset)))
        {
            ltv=edict_getline(edict,stdin);
        }
        else if (ltv->flags&LT_FILE)
        {
            LTV *newltv=edict_getline(edict,ltv->data);
            if (newltv)
            {
                LTV_put(&edict->code,ltv,0,(void *) strspn((char *) ltv->data,WHITESPACE));
                ltv=newltv;
            }
            else
            {
                fclose(ltv->data);
                LTV_release(ltv);
                goto read;
            }
        }

        TRY(!ltv,-1,done,"\n");

        token=ltv->data+(long) offset;
        if (*token) // not end of string
        {
            int nest=0;
            switch (*token)
            {
                case '\'':
                    len=strcspn((token+1)+1,edict->bcdel);
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
                    ops=strspn(token,edict->bc);
                    len=ops+strcspn(token+ops,edict->bcdel);
                    break;
            }
        }

        offset+=len+strspn(token+len,WHITESPACE);

        if ((int) offset < ltv->len)
            LTV_put(&edict->code,ltv,0,(void *) offset);

        if (len)
        {
            int i;
            if (ops)
                for (i=0;i<ops && i<len && edict->bcf[token[i]](edict,token+ops,len-ops);i++);
            else
                edict_ref(edict,token,len,0,0,NULL);
        }

        if ((int) offset >= ltv->len)
            LTV_release(ltv);
    } while (1);
    
 done:
    return status;
}

int bc_slit(EDICT *edict,char *name,int len)
{
    edict_add(edict,LTV_new(name,len,LT_DUP),NULL);
    return 1;
}

int bc_lit(EDICT *edict,char *name,int len)
{
    return bc_slit(edict,name,len-1);
}

int bc_name(EDICT *edict,char *name,int len)
{
    edict_name(edict,name,len,0,NULL);
    return 0;
}

int bc_kill(EDICT *edict,char *name,int len)
{
    void *md;
    len?
        LTV_release(edict_get(edict,name,len,1,0,&md)):
        LTV_release(edict_rem(edict,&md));
    return 0;
}

int bc_exec_enter(EDICT *edict,char *name,int len)
{
    bc_name(edict,"continuation",-1);
    return 1;
}

int bc_exec_leave(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_put(&edict->code,edict_get(edict,"continuation",-1,1,0,&md),0,NULL);
    return 1;
}

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
    stpcpy(stpcpy(edict->bcdel,WHITESPACE),edict->bc);
    return 0;
}

int edict_bytecodes(EDICT *edict)
{
    int i;
    
    for (i=0;i<256;i++)
        edict->bcf[i]=0;
    
    edict_bytecode(edict,'\'',bc_slit);
    edict_bytecode(edict,'[',bc_lit);
    edict_bytecode(edict,'@',bc_name);
    edict_bytecode(edict,'/',bc_kill);
    edict_bytecode(edict,'(',bc_exec_enter);
    edict_bytecode(edict,')',bc_exec_leave);
}


int edict_init(EDICT *edict,LTV *root)
{
    int rval=0;
    BZERO(*edict);
    LT_init();
    TRY(!edict,-1,done,"\n");
    TRY(!(CLL_init(&edict->code)),-1,done,"\n");
    TRY(!(CLL_init(&edict->anon)),-1,done,"\n");
    TRY(!(CLL_init(&edict->dict)),-1,done,"\n");
    LTV_put(&edict->dict,root,0,NULL);
    edict->nil=LTV_new("nil",3,LT_RO);
    edict_bytecodes(edict);
    //LTV_put(&edict->code,LTV_new(fopen("/tmp/jj.in","r"),0,LT_FILE),0,NULL);

 done:
    return rval;
}

int edict_destroy(EDICT *edict)
{
    int rval;
    TRY(!edict,-1,done,"\n");
    CLL_release(&edict->code,LTVR_release);
    CLL_release(&edict->anon,LTVR_release);
    CLL_release(&edict->dict,LTVR_release);
 done:
    return rval;
}

