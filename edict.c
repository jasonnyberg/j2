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

LTV *edict_get(EDICT *edict,char *name,int len,int pop,void **metadata)
{
    void *internal_get(CLL *cll,void *data)
    {
        void *md;
        int tlen;
        LTI *lti;
        int end;
        LTVR *ltvr=(LTVR *) cll; // each CLL node is really an LTVR
        LTV *root=ltvr->ltv;
        
        if (len<0) len=strlen(name);
        do
        {
            tlen=edict_delimit(name,len);
            end=(*name=='-');
            if (!(lti=LT_lookup(&root->rbr,name+end,tlen-end,0)))
                break;
            pop=pop && !(root->flags&LT_RO);
            if (name[tlen]!='.')
            {
                LTV *rval=LTV_get(&lti->cll,pop,end,metadata);
                if (pop && CLL_EMPTY(&lti->cll))
                    RBN_release(&root->rbr,&lti->rbn,LTI_release);
                return rval;
            }
            root=LTV_get(&lti->cll,0,end,&md);
            len-=(tlen+1);
            name+=(tlen+1);
        } while(len>0 && root);
        return NULL;
    }

    LTV *result=CLL_traverse(&edict->dict,0,internal_get,NULL);
    return result?result:edict->nil;
}

LTV *edict_nameltv(EDICT *edict,char *name,int len,void *metadata,LTV *ltv)
{
    void *md;
    int tlen;
    LTI *lti;
    int end;
    LTV *root=LTV_get(&edict->dict,0,0,&md);
        
    if (len<0) len=strlen(name);
    do
    {
        tlen=edict_delimit(name,len);
        end=(*name=='-');
        if ((lti=LT_lookup(&root->rbr,name+end,tlen-end,1)))
        {
            if (name[tlen]!='.')
                return LTV_put(&lti->cll,ltv?ltv:edict->nil,end,metadata);
            
            if (!(root=LTV_get(&lti->cll,0,end,&md)))
                root=LTV_put(&lti->cll,LTV_new("",-1,0),end,NULL);
            len-=(tlen+1);
            name+=(tlen+1);
        }
    } while(len>0 && root);
    return NULL;
}

LTV *edict_add(EDICT *edict,LTV *ltv,void *metadata)
{
    return edict_nameltv(edict,"",0,metadata,ltv);
}

LTV *edict_rem(EDICT *edict,void **metadata)
{
    return edict_get(edict,"",0,1,metadata); // cleanup: LTV_release(LTV *)
}

LTV *edict_name(EDICT *edict,char *name,int len,void *metadata)
{
    void *md;
    return edict_nameltv(edict,name,len,metadata,edict_rem(edict,&md));
}

LTV *edict_ref(EDICT *edict,char *name,int len,int pop,void *metadata)
{
    void *md;
    LTV *ltv=edict_get(edict,name,len,pop,&md);

#ifndef DYNCALL // FIXME: tear this shit out when dyncall is integrated
    if (!strncmp(name,"dump",len))
    {
        LTV_release(ltv);
        edict_dump(edict);
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
                    ops=strspn(token,edict->bc);
                    len=ops+strcspn(token+ops,edict->bcdel);
                    break;
            }
        }

        offset+=len+strspn(token+len,WHITESPACE);

        if ((ull) offset < ltv->len)
            LTV_put(&edict->code,ltv,0,(void *) offset);

        if (len)
        {
            int i;
            if (ops)
                for (i=0;i<ops && i<len && edict->bcf[token[i]](edict,token+ops,len-ops);i++);
            else
                edict_ref(edict,token,len,0,NULL);
        }

        if ((ull) offset >= ltv->len)
            LTV_release(ltv);
    } while (1);
    
 done:
    return status;
}

int bc_slit(EDICT *edict,char *name,int len)
{
    edict_add(edict,LTV_new(name,len,LT_DUP),NULL);
    return 0;
}

int bc_lit(EDICT *edict,char *name,int len)
{
    return bc_slit(edict,name,len-1);
}

int bc_name(EDICT *edict,char *name,int len)
{
    edict_name(edict,name,len,NULL);
    return 1;
}

int bc_kill(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_release(edict_get(edict,name,len,1,&md));
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
    LTV_release(LTV_get(&edict->dict,1,0,&md));
    return 0;
}

int bc_exec_enter(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_put(&edict->dict,LTV_new("",0,LT_DUP),0,NULL);
    bc_name(edict,"_cont",-1);
    return 0;
}

int bc_exec_leave(EDICT *edict,char *name,int len)
{
    void *md;
    LTV_put(&edict->code,LTV_new(">",1,LT_DUP),0,NULL); // close
    LTV_put(&edict->code,LTV_new("^",1,LT_DUP),0,NULL); // splice
    LTV_put(&edict->code,edict_get(edict,"_cont",-1,1,&md),0,NULL);
    return 0;
}

int bc_splice(EDICT *edict,char *name,int len)
{
    LTI *anons[2] = { NULL, NULL };
    int i=0;
    
    void *get_anons(CLL *cll,void *data)
    {
        LTV *root=((LTVR *) cll)->ltv;
        anons[i]=LT_lookup(&root->rbr,"",0,i); // insert LTI on 2nd layer, i.e. i=1 (sweet)
        return i++; // returns halt after populating anons[0] and anons[1]
    }

    CLL_traverse(&edict->dict,0,get_anons,NULL);
    if (anons[0] && anons[1])
        CLL_splice(&(anons[1]->cll),&(anons[0]->cll),0);
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
    edict_bytecode(edict,'<',bc_namespace_enter);
    edict_bytecode(edict,'>',bc_namespace_leave);
    edict_bytecode(edict,'(',bc_exec_enter);
    edict_bytecode(edict,')',bc_exec_leave);
    edict_bytecode(edict,'^',bc_splice);
}


int edict_init(EDICT *edict,LTV *root)
{
    int rval=0;
    BZERO(*edict);
    LT_init();
    TRY(!edict,-1,done,"\n");
    TRY(!(CLL_init(&edict->code)),-1,done,"\n");
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
    CLL_release(&edict->dict,LTVR_release);
 done:
    return rval;
}

