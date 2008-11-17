#include "util.h"
#include "listree.h"

//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

CLL *CLL_init(CLL *lst) { lst->lnk[0]=lst->lnk[1]=lst; }

CLL *CLL_put(CLL *lst,CLL *lnk,int tail)
{
    if (!lst || !lnk) return NULL;
    lnk->lnk[tail]=lst->lnk[tail];
    lnk->lnk[!tail]=lst;
    lnk->lnk[tail]->lnk[!tail]=lst->lnk[tail]=lnk;
    return lnk;
}

CLL *CLL_get(CLL *lst,CLL *lnk,int tail,int pop)
{
    if (!lst || ((lnk=lnk?lnk:lst->lnk[tail]) == lst)) return NULL;
    if (pop)
    {
        lnk->lnk[tail]->lnk[!tail]=lnk->lnk[!tail];
        lnk->lnk[!tail]->lnk[tail]=lnk->lnk[tail];
    }
    return lnk;
}

CLL *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data)
{
    CLL *result,*lnk=lst->lnk[reverse];
    while (lnk && !(result=op(lnk,data)))
        lnk=lnk->lnk[reverse];
    return result;
}


//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

// create a new LTI and prepare for insertion
LTI *lti_new(char *name)
{
    LTI *lti=NEW(LTI);
    lti->name=strdup(name);
    CLL_init(&LTILST(lti));
}

// return node that owns "name", inserting if requested AND required.
LTI *lt_get(LTR *ltr,char *name,int insert)
{
    LTI **lti = (LTI **) &LTRROOT(ltr);
    while (*lti)
    {
        int result = strcmp(name,LTINAME(*lti));
        if (!result) return *lti;
        else lti=(result<0)? &LTILEFT(*lti):&LTIRIGHT(*lti);
    }
    if (push)
    {
        LTI *new=lti_new(name);
        rb_link_node(LTILNK(new),LTIPARENT(lti),lti); // add
        rb_insert_color(LTILNK(new),ltr); // rebalance
        return new;
    }
    else
    {
        return NULL;
    }
}
