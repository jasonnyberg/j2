#include "stdio.h"
#include "util.h"
#include "listree.h"


CLL ltv_repo,ltvr_repo,lti_repo;


//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

CLL *CLL_init(CLL *lst) { return lst->lnk[0]=lst->lnk[1]=lst; }

void CLL_release(CLL *lst,void (*cll_release)(CLL *lnk))
{
    CLL *cll;
    while (cll=CLL_get(lst,0,1)) cll_release(cll);
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

RBR *RBR_init(RBR *rbr)
{
    static int repo_init=0;

    if (!repo_init)
    {
        CLL_init(&ltv_repo);
        CLL_init(&ltvr_repo);
        CLL_init(&lti_repo);
    }
    
    RB_EMPTY_ROOT(rbr);
    return rbr;
}

void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn))
{
    RBN *rbn;
    while (rbn=rbr->rb_node)
    {
        rb_erase(rbn,rbr);
        rbn_release(rbn);
    }
}

void *RBR_traverse(RBR *rbr,LT_OP op,void *data)
{
    RBN *result,*rbn=rb_first(rbr);
    while (rbn && !(result=op(rbn,data))) rbn=rb_next(rbn);
    return result;
}


// get a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,int flags)
{
    LTV *ltv=NULL;
    if (data &&
        ((ltv=(LTV *) CLL_get(&ltv_repo,0,1)) || (ltv=NEW(LTV))))
    {
        ltv->data=flags&LT_DUP?data:bufdup(data,flags&LT_STR?strlen((char *) data):len);
        ltv->flags=flags;
    }
    return ltv;
}

void LTV_free(LTV *ltv)
{
    CLL_put(&ltv_repo,&ltv->repo[0],0);
}


// get a new LTVR
LTVR *LTVR_new()
{
    LTVR *ltvr=(LTVR *) CLL_get(&ltvr_repo,0,1);
    if (!ltvr) ltvr=NEW(LTVR);
    return ltvr;
}

void LTVR_free(LTVR *ltvr)
{
    CLL_put(&ltvr_repo,&ltvr->repo[0],0);
}


// get a new LTI and prepare for insertion
LTI *LTI_new(char *name)
{
    LTI *lti;
    if (name &&
        ((lti=(LTI *) CLL_get(&lti_repo,0,1)) || (lti=NEW(LTI))))
    lti->name=strdup(name);
    CLL_init(&lti->cll);
    return lti;
}

void LTI_free(LTI *lti)
{
    CLL_put(&lti_repo,&lti->repo[0],0);
}



LTV *LTV_put(CLL *cll,LTV *ltv,int end)
{
    LTVR *ltvr=LTVR_new();
    if (cll && ltv && ltvr)
    {
        CLL_put(cll,(CLL *) ltvr,end);
        ltvr->ltv=ltv;
        ltv->refs++;
    }
    return ltvr?ltv:NULL;
}

LTV *LTV_get(CLL *cll,int pop,int end)
{
    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (cll && (ltvr=(LTVR *) CLL_get(cll,pop,end)));
    {
        ltv=ltvr->ltv;
        ltv->refs--;
        if (pop) LTVR_free(ltvr);
    }
    return ltv;
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
        rb_link_node(&lti->rbn,*rbn?rb_parent(*rbn):NULL,rbn); // add
        rb_insert_color(&lti->rbn,rbr); // rebalance
    }
    return lti;
}


//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && ltv->refs--<=1)
    {
        RBR_release(&ltv->subs,LTI_release);
        if (ltv->flags&LT_DUP) DELETE(ltv->data);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *cll)
{
    LTVR *ltvr=(LTVR *) cll;
    if (ltvr)
    {
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

