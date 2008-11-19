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

void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data)
{
    CLL *result,*lnk=lst->lnk[reverse];
    while (lnk && lnk!=lst && !(result=op(lnk,data)))
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
    CLL_init(&LTICLL(lti));
}

LTV *ltv_new(char *data)
{
    LTV *ltv=NEW(LTV);
    ltv->data=strdup(data);
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

void *lt_traverse(struct rb_root *ltr,LT_OP op,void *data)
{
    struct rb_node *result,*ltn=rb_first(ltr);
    while (ltn && !(result=op(ltn,data)))
        ltn=rb_next(ltn);
    return NULL;
}



void *lt_dump_ltv(CLL *lnk,void *data)
{
    printf("  %s\n",LTVDATA((LTV *) lnk));
    return NULL;
}

void *lt_dump_rbn(struct rb_node *rbn,void *data)
{
    printf("%s:\n",LTINAME(rbn));
    return CLL_traverse(&LTICLL(rbn),0,lt_dump_ltv,data);
}

void *lt_dump(struct rb_root *ltr,void *data)
{
    return lt_traverse(ltr,lt_dump_rbn,data);
}
