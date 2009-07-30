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

LTV *edict_add(EDICT *edict,LTV *ltv,void *metadata)
{
    return LTV_put(&edict->anon,ltv,0,metadata);
}

LTV *edict_rem(EDICT *edict,void **metadata)
{
    return LTV_get(&edict->anon,1,0,metadata); // cleanup: LTV_release(LTV *)
}

LTV *edict_name(EDICT *edict,char *name,int len,int end,void *metadata)
{
    void *md;
    LTV *root=LTV_get(&edict->dict,0,0,&md);
    if (len<0) len=strlen(name);
    while(len>0 && root)
    {
        LTI *lti;
        int tlen=edict_delimit(name,len);
        if ((lti=LT_lookup(&root->rbr,name,tlen,1)))
        {
            if (name[tlen]=='.')
                root=LTV_get(&lti->cll,0,0,&md);
            else if (name[tlen]==',')
                root=LTV_get(&lti->cll,0,1,&md);
            else
                return LTV_put(&lti->cll,LTV_get(&edict->anon,1,0,&md),end,metadata);
            if (!root)
                root=LTV_put(&lti->cll,LTV_new("",-1,0),0,NULL);
            len-=(tlen+1);
            name+=(tlen+1);
        }
    }
    return NULL;
}


LTV *edict_get(EDICT *edict,char *name,int len,int pop,int end,void **metadata)
{
    void *md;
    LTV *root=LTV_get(&edict->dict,0,0,&md);
    if (len<0) len=strlen(name);
    while(len>0 && root)
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
                if (CLL_EMPTY(&lti->cll))
                    RBN_release(&root->rbr,&lti->rbn,LTI_release);
                return rval;
            }
            len-=(tlen+1);
            name+=(tlen+1);
        }
        else return edict->nil;
    }
    return NULL;
}

LTV *edict_ref(EDICT *edict,char *name,int len,int pop,int end,void *metadata)
{
    void *md;
    LTV *ltv=edict_get(edict,name,len,pop,end,&md);
    return ltv?LTV_put(&edict->anon,ltv,0,metadata):NULL;
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
    
    return totlen?LTV_new(rbuf,totlen,0):NULL;
}


int edict_repl(EDICT *edict)
{
    int status;
    void *offset;
    LTV *ltv;
    char *token;
    int len;
        
    do
    {
        status=0;
        offset=0;
        ltv=NULL;
        token=NULL;
        len=0;
        
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
                LTV_put(&edict->code,ltv,0,NULL);
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
        if (*token && *(token+=strspn(token,WHITESPACE))) // not end of string
        {
            int nest=0;
            switch (*token)
            {
                case '\'':
                    len=strcspn((token)+1,WHITESPACE)+1;
                    break;
                case '[':
                    len=edict_balance(token,"[]",&nest); // lit
                    break;
                case '(':
                case ')':
                case '{':
                case '}':
                case '<':
                case '>':
                    len=1;
                    break;
                default: // expression
                    len=strcspn(token,LIT_DELIMIT);
                    break;
            }
            
            LTV_put(&edict->code,ltv,0,(void *) (token-(char *) ltv->data)+len);
        }
        else
        {
            DELETE(ltv->data);
            LTV_release(ltv);
        }

        if (len)
        {
            int i,ops,status=0;
            int bc_ref()
            {
                return !edict_ref(edict,token,len,0,0,NULL);
            }
            int bc_ops()
            {
                for (i=0;i<ops;i++)
                    if (edict_bcf[token[i]](edict,token+ops,len-ops))
                        break;
                return status;
            }

            switch (token[0])
            {
                case '\'': edict_add(edict,LTV_new(token+1,len,LT_DUP),NULL); break;
                case '[': edict_add(edict,LTV_new(token+1,len-2,LT_DUP),NULL); break;
                default:
                    TRY((ops=strspn(token,edict_bc))>len,0,done,"Invalid token\n");
                    TRY((status=ops?bc_ops():bc_ref()),status,done,"\n");
                    break;
            }
        }
        
        edict_dump(edict);
    } while (1);
    
 done:
    return status;
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
        LTV_release(LTV_get(&edict->anon,1,0,&md));
    return 0;
}

int bc_exec_enter(EDICT *edict,char *name,int len)
{
    printf("edict_exec_enter: ");
    fstrnprint(stdout,name,len);
    printf("\n");
    return 0;
}

int bc_exec_leave(EDICT *edict,char *name,int len)
{
    printf("edict_exec_leave: ");
    fstrnprint(stdout,name,len);
    printf("\n");
    return 0;
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
    edict_bc[numbc++]=bc;
    edict_bc[numbc]=0;
    edict_bcf[bc]=bcf;
    return 0;
}

int edict_bytecodes(EDICT *edict)
{
    int i;
    
    for (i=0;i<256;i++)
        edict_bcf[i]=0;
    
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


