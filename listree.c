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
LTI *lt_get(struct rb_root *ltr,char *name,int insert)
{
    struct rb_node **rbn = &(ltr->rb_node);
    while (*rbn)
    {
        int result = strcmp(name,LTINAME(*rbn));
        if (!result) return (LTI *) *rbn;
        // else lti=(result<0)? (&LTILEFT(*lti)):(&LTIRIGHT(*lti));
        else rbn=(result<0)?
            &((*rbn)->rb_left):&((*rbn)->rb_right);
        
        
    }
    if (insert)
    {
        LTI *new=lti_new(name);
        rb_link_node(&LTILNK(new),rb_parent(*rbn),rbn); // add
        rb_insert_color(&LTILNK(new),ltr); // rebalance
        return new;
    }
    else
    {
        return NULL;
    }
}
