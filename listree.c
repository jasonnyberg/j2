#include "stdio.h"
#include "util.h"
#include "listree.h"


//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

CLL *CLL_init(CLL *lst) { return lst->lnk[0]=lst->lnk[1]=lst; }

void CLL_release(CLL *lst,void (*cll_release)(CLL *lnk,void *data),void *data) 
{
    CLL *cll;
    while (cll=CLL_get(lst,0,1)) cll_release(cll,data);
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
    CLL *result,*lnk=lst->lnk[reverse];
    while (lnk && lnk!=lst && !(result=op(lnk,data))) lnk=lnk->lnk[reverse];
    return result;
}

#define CLL_EMPTY(lst) (CLL_get(lst,NULL,0,0))


//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

RBR *RBR_init(RBR *rbr) { RB_EMPTY_ROOT(rbr); return rbr; }

void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn,void *data),void *data)
{
    RBN *rbn;
    while (rbn=rbr->rb_node)
    {
        rb_erase(rbn,rbr);
        rbn_release(rbn,data);
    }
}

void *RBR_traverse(RBR *rbr,LT_OP op,void *data)
{
    RBN *result,*rbn=rb_first(rbr);
    while (rbn && !(result=op(rbn,data))) rbn=rb_next(rbn);
    return result;
}

// create a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,int flags)
{
    LTV *ltv=NEW(LTV);
    ltv->data=flags&LT_DUP?data:bufdup(data,flags&LT_STR?strlen((char *) data):len);
    ltv->flags=flags;
    return ltv;
}

LTV *LTV_put(CLL *trash,CLL *cll,LTV *ltv,int end)
{
    LTVR *ltvr=NULL;
    if (trash && cll && ltv &&
        ((ltvr=(LTVR *) CLL_get(trash,0,1)) || (ltvr=NEW(LTVR))))
    {
        CLL_put(cll,(CLL *) ltvr,0);
        ltvr->ltv=ltv;
        ltv->refs++;
    }
    return ltvr?ltv:NULL;
}

LTV *LTV_get(CLL *trash,CLL *cll,int pop,int end)
{
    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (trash && cll && (ltvr=(LTVR *) CLL_get(cll,pop,0)));
    {
        ltv=ltvr->ltv;
        ltv->refs--;
        if (pop) CLL_put(trash,&ltvr->cll,0);
    }
    return ltv;
}

// create a new LTI and prepare for insertion
LTI *LTI_new(char *name)
{
    LTI *lti=NEW(LTI);
    lti->name=strdup(name);
    CLL_init(&lti->cll);
    return lti;
}


// return node that owns "name", inserting if desired AND required.
LTI *LT_lookup(RBR *rbr,char *name,int insert)
{
    RBN **rbn = &(rbr->rb_node);
    LTI *lti=NULL;
    
    while (*rbn)
    {
        int result = strcmp(name,((LTI *) *rbn)->name);
        if (!result) return (LTI *) *rbn; // found it!
        else rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right;
    }
    if (insert && (lti=LTI_new(name)))
    {
        rb_link_node(&lti->u.rbn,*rbn?rb_parent(*rbn):NULL,rbn); // add
        rb_insert_color(&lti->u.rbn,rbr); // rebalance
    }
    return lti;
}


//////////////////////////////////////////////////
// LT Free Tag Team (frees memory)
//////////////////////////////////////////////////

void LTVR_free(CLL *cll,void *data)
{
    LTVR *ltvr=(LTVR *) cll;
    if (ltvr)
    {
        LTV *ltv=ltvr->ltv;
        DELETE(ltvr);
        if (ltv && ltv->refs--<=1)
        {
            RBR_release(&ltv->subs,LTI_free,data);
            if (ltv->flags&LT_DUP) DELETE(ltv->data);
            DELETE(ltv);
        }
    }
}

void LTI_free(RBN *rbn,void *data)
{
    LTI *lti=(LTI *) rbn;
    if (lti)
    {
        CLL_release(&lti->cll,LTVR_free,data);
        DELETE(lti->name);
        DELETE(lti);
    }
}

