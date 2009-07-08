#define _GNU_SOURCE
#include <stdlib.h>
#include "util.h"
#include "edict.h"


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

int edict_init(EDICT *edict,LTV *root)
{
    int rval=0;
    LT_init();
    TRY(!edict,-1,done,"\n");
    TRY(!(CLL_init(&edict->code)),-1,done,"\n");
    TRY(!(CLL_init(&edict->anon)),-1,done,"\n");
    TRY(!(CLL_init(&edict->dict)),-1,done,"\n");
    LTV_put(&edict->dict,root,0,NULL);
    //LTV_put(&edict->code,LTV_new(fopen("/tmp/jj.in","r"),0,LT_FILE),0,NULL);

    strcpy(edict->delimiter[DELIMIT_SIMPLE_LIT_END],LIT_DELIMIT);
    strcpy(edict->delimiter[DELIMIT_EXP_END],LIT_DELIMIT);

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


LTV *edict_ref(EDICT *edict,char *name,int len,int pop,int end,void *metadata)
{
    void *md;
    LTV *root=LTV_get(&edict->dict,0,0,&metadata);
    if (len<0) len=strlen(name);
    while(len>0 && root)
    {
        LTI *lti;
        int tlen=edict_delimit(name,len);
        if ((lti=LT_lookup(&root->rbr,name,tlen,0)))
        {
            if (name[tlen]=='.')
                root=LTV_get(&lti->cll,0,0,&md);
            else if (name[tlen]==',')
                root=LTV_get(&lti->cll,0,1,&md);
            else
                return LTV_put(&edict->anon,LTV_get(&lti->cll,pop,end,&md),0,metadata);
            len-=(tlen+1);
            name+=(tlen+1);
        }
    }
    return NULL;
}

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// PARSER
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

#define DICT_BYTECODE(edict,opcode,fn)                                                                    \
    {                                                                                                     \
        edict_bytecodes.bytecode[(int) *opcode]=(edict_extension) ((unsigned) *fn);                       \
        if (!strchr(delimiter[DELIMIT_EXP_START],*opcode))                                                \
        {                                                                                                 \
            delimiter[DELIMIT_EXP_START][edict_bytecode_count++]=*opcode;                                 \
            delimiter[DELIMIT_EXP_START][edict_bytecode_count]=(char) NULL;                               \
            stpcpy(stpcpy(delimiter[DELIMIT_SIMPLE_LIT_END],WHITESPACE),delimiter[DELIMIT_EXP_START]);    \
            stpcpy(stpcpy(delimiter[DELIMIT_EXP_END],LIT_DELIMIT),delimiter[DELIMIT_SIMPLE_LIT_END]);     \
        }                                                                                                 \
    }


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


int edict_read(EDICT *edict,char **token,int *len)
{
    int status=0;
    void *offset=0;
    LTV *ltv=NULL;

    *len=0;
    *token=NULL;
    
 getcode:
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
            goto getcode;
        }
    }

    TRY(!ltv,-1,done,"\n");

    *token=ltv->data+(long) offset;
    if (**token && *(*token+=strspn(*token,WHITESPACE))) // not end of string
    {
        int nest=0;
        switch (**token)
        {
            case '\'':
                *len=strcspn((*token)+1,edict->delimiter[DELIMIT_SIMPLE_LIT_END])+1;
                break;
            case '[':
                *len=edict_balance(*token,"[]",&nest); // lit
                break;
            case '(':
            case ')':
            case '{':
            case '}':
            case '<':
            case '>':
                *len=1;
                break;
            default: // expression
                *len=strcspn(*token,edict->delimiter[DELIMIT_EXP_END]);
                break;
        }
            
        LTV_put(&edict->code,ltv,0,(void *) (*token-(char *) ltv->data)+*len);
    }
    else
    {
        DELETE(ltv->data);
        LTV_release(ltv);
    }

 done:
    return status;
}

int strnprint(char *str,int len)
{
    printf("%d:\"",len);
    while(len--) putchar(*str++);
    printf("\"\n");
}

int edict_eval(EDICT *edict,char *token,int len)
{
    strnprint(token,len);
    
    return 0;
}

int edict_thread(EDICT *edict)
{
    int status,len;
    char *token;
    
    while (1)
    {
        TRY((status=edict_read(edict,&token,&len)),status,done,"\n");
        TRY((status=edict_eval(edict,token,len)),status,done,"\n");
    }
    
 done:
    return status;
}


/*
void *edict_pop(EDICT *edict,char *name,int len,int end)
{
}

LTV *edict_clone(LTV *ltv,int sibs)
{
}
int edict_copy_item(EDICT *edict,LTV *ltv)
{
}
int edict_copy(EDICT *edict,char *name,int len)
{
}
int edict_raise(EDICT *edict,char *name,int len)
{
}

void *edict_lookup(EDICT *edict,char *name,int len)
{
}

void edict_display_item(LTV *ltv,char *prefix)
{
}
void edict_list(EDICT *edict,char *buf,int len,int count,char *prefix)
{
}

int edict_len(EDICT *edict,char *buf,int len)
{
}

LTV *edict_getitem(EDICT *edict,char *name,int len,int pop)
{
}
LTV *edict_getitems(EDICT *edict,LTV *repos,int display)
{
}

LTV *edict_get_nth_item(EDICT *edict,int n)
{
}
*/

