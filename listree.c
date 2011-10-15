#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include "util.h"
#include "listree.h"

CLL ltv_repo,ltvr_repo,lti_repo;
int ltv_count=0,ltvr_count=0,lti_count=0;

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
        ltv->data=data;
        if (flags&LT_DUP) (ltv->flags|=LT_DEL),(ltv->data=bufdup(ltv->data,ltv->len));
        if (flags&LT_ESC) strstrip(ltv->data,&ltv->len);
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
            int result = strnncmp(name,len,((LTI *) *rbn)->name,-1);
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

void *RBR_traverse(RBR *rbr,int reverse,RB_OP op,void *data)
{
    RBN *rbn=NULL,*result=NULL;
    if (reverse)
        for (rbn=rb_last(rbr);rbn && !(result=op(rbn,data));rbn=rb_prev(rbn));
    else
        for (rbn=rb_first(rbr);rbn && !(result=op(rbn,data));rbn=rb_next(rbn));
    return result;
}

//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

void *LTV_traverse(LTV *ltv,void *data)
{
    void *rval=NULL;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltv) goto done;
    
    if (!ltobj_data) // remove absoloute visited flag
        return (ltv->flags&LT_AVIS && !((ltv->flags&=~LT_AVIS)&LT_AVIS) && ltv->rbr.rb_node)?
            RBR_traverse(&ltv->rbr,0,LTI_traverse,data):NULL;
    else if (!(ltv->flags&LT_RVIS))
    {
        if (ltobj_data->preop && (rval=ltobj_data->preop(NULL,NULL,ltv,data))) goto done;
        
        ltv->flags|=LT_RVIS;
        ltobj_data->depth++;
        if (!ltobj_data->halt && ltv->rbr.rb_node) rval=RBR_traverse(&ltv->rbr,0,LTI_traverse,data);
        ltobj_data->depth--;
        ltv->flags&=~LT_RVIS;
        if (rval) goto done;
        
        if (ltobj_data->postop && (rval=ltobj_data->postop(NULL,NULL,ltv,data))) goto done;
    }
    
 done:
    if (ltv) ltv->flags|=LT_AVIS;
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

void *LTVR_traverse(CLL *cll,void *data)
{
    void *rval=NULL;
    LTVR *ltvr = (LTVR *) cll;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltvr) goto done;
    
    if (ltobj_data && ltobj_data->preop && (rval=ltobj_data->preop(ltvr,NULL,NULL,data))) goto done;
    if ((!ltobj_data || !ltobj_data->halt) && ltvr->ltv && (rval=LTV_traverse(ltvr->ltv,data))) goto done;
    if (ltobj_data && ltobj_data->postop && (rval=ltobj_data->postop(ltvr,NULL,NULL,data))) goto done;
    
 done:
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

void *LTI_traverse(RBN *rbn,void *data)
{
    void *rval=NULL;
    LTI *lti=(LTI *) rbn;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!lti) goto done;
    
    if (ltobj_data && ltobj_data->preop && (rval=ltobj_data->preop(NULL,lti,NULL,data))) goto done;
    if ((!ltobj_data || !ltobj_data->halt) && (rval=CLL_traverse(&lti->cll,0,LTVR_traverse,data))) goto done;
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

LTV *LTV_get(CLL *cll,int pop,int end,void *match,int matchlen,LTVR **ltvr)
{
    void *ltv_match(CLL *cll,void *data)
    {
        LTVR *ltvr=(LTVR *) cll;
        if (!ltvr || !ltvr->ltv || ltvr->ltv->len!=matchlen || memcmp(ltvr->ltv->data,match,matchlen)) return NULL;
        else return pop?CLL_pop(cll):cll;
    }
    
    LTV *rval=NULL;
    (*ltvr)=NULL;
    if (match && matchlen<0) matchlen=strlen(match);
    if (!((*ltvr)=(LTVR *) match?CLL_traverse(cll,end,ltv_match,NULL):CLL_get(cll,pop,end)))
        return NULL;
    rval=(*ltvr)->ltv;
    rval->refs-=pop;
    if (pop)
    {
        LTVR_free(*ltvr);
        (*ltvr)=NULL;
    }
    return rval;
}

void LT_init()
{
    CLL_init(&ltv_repo);
    CLL_init(&ltvr_repo);
    CLL_init(&lti_repo);
}
