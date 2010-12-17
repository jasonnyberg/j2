#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include "util.h"
#include "listree.h"

//#define PEDANTIC(alt,args...) args
#define PEDANTIC(alt,args...) alt

CLL ltv_repo,ltvr_repo,lti_repo;
int ltv_count=0,ltvr_count=0,lti_count=0;

//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

CLL *CLL_init(CLL *lst) { return lst->lnk[0]=lst->lnk[1]=lst; }

void CLL_release(CLL *lst,void (*op)(CLL *lnk))
{
    CLL *cll;
    while (cll=CLL_get(lst,1,0)) op(cll);
}

CLL *CLL_put(CLL *lst,CLL *lnk,int end)
{
    if (!lst || !lnk) return NULL;
    lnk->lnk[end]=lst->lnk[end];
    lnk->lnk[!end]=lst;
    lnk->lnk[end]->lnk[!end]=lst->lnk[end]=lnk;
    return lnk;
}

CLL *CLL_splice(CLL *dst,CLL *src,int end)
{
    if (!dst || !src || CLL_EMPTY(src)) return NULL;
    CLL *dhead=dst->lnk[end],*shead=src->lnk[end],*stail=src->lnk[!end];
    shead->lnk[!end]=dst;
    stail->lnk[end]=dhead;
    dst->lnk[end]=shead;
    dhead->lnk[!end]=stail;
    src->lnk[end]=src->lnk[!end]=src;
}

CLL *CLL_pop(CLL *lnk)
{
    if (!lnk) return NULL;
    lnk->lnk[0]->lnk[1]=lnk->lnk[1];
    lnk->lnk[1]->lnk[0]=lnk->lnk[0];
    lnk->lnk[0]=lnk->lnk[1]=NULL;
    return lnk;
}

CLL *CLL_get(CLL *lst,int pop,int end)
{
    if (!lst || lst==lst->lnk[end]) return NULL;
    return pop?CLL_pop(lst->lnk[end]):lst->lnk[end];
}

void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data)
{
    CLL *result=NULL,*lnk=lst->lnk[reverse];
    while (lnk && lnk!=lst && !(result=op(lnk,data)))
        lnk=lnk->lnk[reverse];
    return result;
}


//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

RBR *RBR_init(RBR *rbr)
{
    RB_EMPTY_ROOT(rbr);
    return rbr;
}

void RBN_release(RBR *rbr,RBN *rbn,void (*rbn_release)(RBN *rbn))
{
    rb_erase(rbn,rbr);
    rbn_release(rbn);
}

void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn))
{
    RBN *rbn;
    while (rbn=rbr->rb_node)
        RBN_release(rbr,rbn,rbn_release);
}


// get a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,LTV_FLAGS flags)
{
    LTV *ltv=NULL;
    if (!data)
    {
        data="";
        flags|=LT_DUP;
        len=0;
    }
    if ((ltv=(LTV *) CLL_get(&ltv_repo,1,1)) || (ltv=NEW(LTV)))
    {
        ltv_count++;
        ltv->len=len<0?strlen((char *) data):len;
        ltv->flags=flags;
        ltv->data=flags&LT_DUP?ltv->flags|=LT_DEL,bufdup(data,ltv->len):data;
    }
    return ltv;
}

void LTV_free(LTV *ltv)
{
    ZERO(*ltv);
    CLL_put(&ltv_repo,&ltv->repo[0],0);
    ltv_count--;
}


// get a new LTVR
LTVR *LTVR_new(void *metadata)
{
    LTVR *ltvr=(LTVR *) CLL_get(&ltvr_repo,1,1);
    if (ltvr || (ltvr=NEW(LTVR)))
    {
        ltvr->metadata=metadata;
        ltvr_count++;
    }
    return ltvr;
}

void *LTVR_free(LTVR *ltvr)
{
    void *metadata=ltvr->metadata;
    ZERO(*ltvr);
    CLL_put(&ltvr_repo,&ltvr->repo[0],0);
    ltvr_count--;
    return metadata;
}


// get a new LTI and prepare for insertion
LTI *LTI_new(char *name,int len)
{
    LTI *lti;
    if (name && ((lti=(LTI *) CLL_get(&lti_repo,1,1)) || (lti=NEW(LTI))))
    {
        lti_count++;
        lti->name=bufdup(name,len);
        CLL_init(&lti->cll);
    }
    return lti;
}

void LTI_free(LTI *lti)
{
    ZERO(*lti);
    CLL_put(&lti_repo,&lti->repo[0],0);
    lti_count--;
}


// return node that owns "name", inserting if desired AND required.
LTI *LT_find(RBR *rbr,char *name,int len,int insert)
{
    LTI *lti=NULL;
    
    if (rbr && name)
    {
        RBN *parent=NULL,**rbn = &(rbr->rb_node);
        while (*rbn)
        {
            int result = LT_strcmp(name,len,((LTI *) *rbn)->name);
            if (!result) return (LTI *) *rbn; // found it!
            else (parent=*rbn),(rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right);
        }
        if (insert && (lti=LTI_new(name,len)))
        {
            rb_link_node(&lti->rbn,parent,rbn); // add
            rb_insert_color(&lti->rbn,rbr); // rebalance
        }
    }
    return lti;
}

void *RBR_traverse(RBR *rbr,char *pat,unsigned len,RB_OP op,void *data)
{
    RBN *result=NULL,*rbn=NULL;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    int nlen;
    
    if (pat && (nlen=strncspn(pat,len,".=+")) && strncspn(pat,nlen,"*?[]-^")<nlen)
    {
        rbn=LT_find(rbr,pat,nlen,/* (ltobj && ltobj->insert) || */ pat[nlen]=='+'); // insert by fiat or pattern-specified
        result=op(rbn,data);
    }
    else
    {
        for (rbn=rb_first(rbr);
             rbn && ((pat && len && fnmatch_len(pat,((LTI *) *rbn)->name),len) || !(result=op(rbn,data)));
             rbn=rb_next(rbn));
    }
    
    return result;
}

//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

void *LTV_traverse(LTV *ltv,char *pat,unsigned len,void *data)
{
    void *rval=NULL;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    LTV *qualifies(char *pat,int len,LTV *ltv) { return !strnncmp(pat,len,ltv->data,ltv->len); }
    int nlen=0,vlen=0;

    if (!ltv) goto done;

    if (!ltobj_data) // remove absoloute visited flag
        return (ltv->flags&LT_AVIS && !((ltv->flags&=~LT_AVIS)&LT_AVIS) && ltv->rbr.rb_node)?
            RBR_traverse(&ltv->rbr,LTI_traverse,data):NULL;
    else if (!(ltv->flags&LT_RVIS))
    {
        int nlen=0,vlen=0;
        
        if (pat && len)
        {
            vlen=strncspn(pat,len,"=+"); // test for specific value
            nlen=strncspn(pat,len,"."); // end of layer name
            if (vlen>nlen || strnncmp(pat+vlen,nlen-vlen,ltv->data,ltv->len))
                goto done; // ltv fails pattern check
        }
    
        if (ltobj_data->preop && (rval=ltobj_data->preop(NULL,NULL,ltv,data))) goto done;
        
        ltv->flags|=LT_RVIS;
        ltobj_data->depth++;
        if (!(ltobj_data->halt) && ltv->rbr.rb_node)
        {
            if (!pat || len-nlen)
                rval=RBR_traverse(&ltv->rbr,pat+nlen,len-nlen,LTI_traverse,data);
        }
        ltobj_data->depth--;
        ltv->flags&=~LT_RVIS;
        if (rval) goto done;
        
        if (ltobj_data->postop && (rval=ltobj_data->postop(NULL,NULL,ltv,data))) goto done;
    }
    
 done:
    if (ltv && !pat) ltv->flags|=LT_AVIS;
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

void *LTVR_traverse(CLL *cll,char *pat,unsigned len,void *data)
{
    void *rval=NULL;
    LTVR *ltvr = (LTVR *) cll;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltvr) goto done;
    
    if (ltobj_data && ltobj_data->preop && (rval=ltobj_data->preop(ltvr,NULL,NULL,data))) goto done;
    if ((!ltobj_data || !(ltobj_data->halt)) && ltvr->ltv && (rval=LTV_traverse(ltvr->ltv,data))) goto done;
    if (ltobj_data && ltobj_data->postop && (rval=ltobj_data->postop(ltvr,NULL,NULL,data))) goto done;
    
 done:
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

void *LTI_traverse(RBN *rbn,char *pat,unsigned len,void *data)
{
    void *rval=NULL;
    LTI *lti=(LTI *) rbn;
    LTV *ltv=NULL;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    int reverse=pat && len && pat[0]=='-';
    
    if (!lti) goto done;

    if (ltobj_data && ltobj_data->preop && (rval=ltobj_data->preop(NULL,lti,NULL,data))) goto done;
    if (!ltobj_data || ltobj_data->halt) goto done;
    for (CLL *cll=lti->cll.lnk[reverse];cll && cll!=&lti->cll;cll=cll->lnk[reverse]) // See CLL_traverse
        if (rval=LTV_traverse(((LTVR *) cll)->ltv,pat+reverse,len-reverse,data))) goto done;
    if (ltobj_data && ltobj_data->postop && (rval=ltobj_data->postop(NULL,lti,NULL,data))) goto done;
    
 done:
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && !ltv->refs && !(ltv->flags&LT_RO))
    {
        RBR_release(&ltv->rbr,LTI_release);
        if (ltv->flags&LT_DEL) DELETE(ltv->data);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *cll)
{
    LTVR *ltvr=(LTVR *) cll;
    if (ltvr)
    {
        ltvr->ltv->refs--;
        LTV_release(ltvr->ltv);
        LTVR_free(ltvr);
    }
}

void LTI_release(RBN *rbn)
{
    LTI *lti=(LTI *) rbn;
    if (lti)
    {
        CLL_release(&lti->cll,LTVR_release);
        DELETE(lti->name);
        LTI_free(lti);
    }
}


//////////////////////////////////////////////////
// Basic LT insert/remove
//////////////////////////////////////////////////

LTV *LTV_put(CLL *cll,LTV *ltv,int end,void *metadata)
{
    int status=0;
    LTV *rval=NULL;
    LTVR *ltvr;
    TRY(!(cll && ltv && (ltvr=LTVR_new(metadata))),0,done,"cll/ltv/ltvr:0x%x/0x%x/0x%x\n",cll,ltv,ltvr);
    TRY(!CLL_put(cll,(CLL *) ltvr,end),0,done,"CLL_put(...) failed!\n",0);
    rval=ltvr->ltv=ltv;
    rval->refs++;
 done:
    return rval;
}

LTV *LTV_get(CLL *cll,int pop,int end,void *match,int matchlen,void **metadata)
{
    void *ltv_match(CLL *cll,void *data)
    {
        LTVR *ltvr=(LTVR *) cll;
        if (!ltvr || !ltvr->ltv || ltvr->ltv->len!=matchlen || memcmp(ltvr->ltv->data,match,matchlen)) return NULL;
        else return pop?CLL_pop(cll):cll;
    }
    
    LTV *rval=NULL;
    LTVR *ltvr=NULL;
    if (match && matchlen<0) matchlen=strlen(match);
    if (!(ltvr=(LTVR *) match?CLL_traverse(cll,end,ltv_match,NULL):CLL_get(cll,pop,end)))
        return NULL;
    rval=ltvr->ltv;
    rval->refs-=pop;
    *metadata=ltvr->metadata;
    if (pop) LTVR_free(ltvr);
    return rval;
}

void LT_init()
{
    CLL_init(&ltv_repo);
    CLL_init(&ltvr_repo);
    CLL_init(&lti_repo);
}
