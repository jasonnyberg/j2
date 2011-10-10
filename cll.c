#include "cll.h"

#include <stdlib.h>

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

// prepend or postpend src to dest
CLL *CLL_splice(CLL *dst,int end,CLL *src)
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

void *CLL_traverse(CLL *lst,int end,CLL_OP op,void *data)
{
    CLL *result=NULL,*next=NULL,*lnk=lst->lnk[end];
    while (lnk && lnk!=lst && next=lnk->lnk[end] && !(result=op(lnk,data)))
        lnk=next;
    return result;
}
