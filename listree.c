#include "stdio.h"
#include "util.h"
#include "listree.h"


//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

CLL *CLL_init(CLL *lst) { return lst->lnk[0]=lst->lnk[1]=lst; }

void CLL_destroy(CLL *lst,void (*cll_free)(CLL *lnk)) 
{
    CLL *cll;
    while (cll=CLL_get(lst,0,1)) cll_free(cll);
}

CLL *CLL_put(CLL *lst,CLL *lnk,int end)
{
    if (!lst || !lnk) return NULL;
    lnk->lnk[end]=lst->lnk[end];
    lnk->lnk[!end]=lst;
    lnk->lnk[end]->lnk[!end]=lst->lnk[end]=lnk;
    return lnk;
}

CLL *CLL_pop(CLL *lnk)
{
    if (!lnk) return NULL;
    lnk->lnk[end]->lnk[!end]=lnk->lnk[!end];
    lnk->lnk[!end]->lnk[end]=lnk->lnk[end];
    lnk->lnk[end]=lnk->lnk[!end]=NULL;
    return lnk;
}

CLL *CLL_get(CLL *lst,int pop,int end)
{
    if (!lst || lst==(lnk?lnk:lnk=lst->lnk[end])) return NULL;
    return pop?CLL_pop(lnk):lnk;
}

void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data)
{
    CLL *result,*lnk=lst->lnk[reverse];
    while (lnk && lnk!=lst && !(result=op(lnk,data))) lnk=lnk->lnk[reverse];
    return result;
}

#define CLL_EMPTY(lst) (CLL_get(lst,NULL,0,0))


//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

RBR *RBR_init(RBR *rbr) { RB_EMPTY_ROOT(rbr); return rbr; }

void RBR_destroy(RBR *rbr,void (*rbn_free)(RBN *rbn)) 
{
    RBN *rbn;
    while (rbn=(RBN *) RBROOT(rbr)) rbn_free(rbn);
}

void *LT_traverse(RBR *rbr,LT_OP op,void *data)
{
    RBN *result,*rbn=rb_first(rbr);
    while (rbn && !(result=op(rbn,data))) rbn=rb_next(rbn);
    return result;
}

// create a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,int flags)
{
    LTV *ltv=NEW(LTV);
    ltv->data=flags&LT_DUP?data:bufdup(data,len);
    ltv->flags=flags;
    return ltv;
}

void LTV_free(CLL *cll)
{
    LTV *ltv=(LTV *) cll;
    if (ltv && ltv->refs--<=1)
    {
        RBR_destroy(ltv->subs,lti_free);
        if (ltv->flags&LT_DUP) DELETE(ltv->data);
        DELETE(ltv);
    }
}

// create a new LTI and prepare for insertion
LTI *LTI_new(char *name)
{
    LTI *lti=NEW(LTI);
    lti->name=strdup(name);
    CLL_init(&lti->cll);
    return lti;
}

void LTI_free(RBN *rbn)
{
    LTI *lti=(LTI *) rbn;
    if (lti)
    {
        CLL_destroy(lti->cll);
        DELETE(lti->name);
        DELETE(lti);
    }
}

LTV *LTI_assign(LTI *lti,LTV *ltv,int end)
{
    if (lti && ltv)
    {
        return CLL_put(&lti->cll,(CLL *) ltv,end);
        ltv->refs++;
    }
}

// return node that owns "name", inserting if desired AND required.
LTI *LT_lookup(RBR *rbr,char *name,int insert)
{
    RBN **rbn = &(rbr->rb_node);
    LTI *lti=NULL;
    
    while (*rbn)
    {
        int result = strcmp(name,LTINAME(*rbn));
        if (!result) return (LTI *) *rbn; // found it!
        else rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right;
    }
    if (insert && lti=LTI_new(name))
    {
        rb_link_node((RBN *)new,*rbn?rb_parent(*rbn):NULL,rbn); // add
        rb_insert_color((RBN *)new,rbr); // rebalance
    }
    return lti;
}


//////////////////////////////////////////////////
// Value Stack
//////////////////////////////////////////////////


//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

