#ifndef LISTREE_H
#define LISTREE_H

//////////////////////////////////////////////////
// Circular list (StaQ) (sentinel implementation)
//////////////////////////////////////////////////

// head=next[0],tail=next[1]
typedef struct CLL { struct CLL *lnk[2]; } CLL;
typedef void *(*CLL_OP)(CLL *lnk,void *data);

extern CLL *CLL_init(CLL *lst);
extern CLL *CLL_put(CLL *lst,CLL *lnk,int tail);
extern CLL *CLL_get(CLL *lst,CLL *lnk,int tail,int pop);
extern void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data);


//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

#include "rbtree.h"

#define LTR struct rb_root
#define LTN struct rb_node

typedef struct
{
    CLL lnk;
    int flags;
    void *data;
    int len;
    int refs;
    LTR subs;
} LTV; // LisTree Value

typedef struct
{
    LTN lnk;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*LT_OP)(LTN *ltn,void *data);

#define LTRROOT(rbr)            ((rbr)->rb_node)
#define LTRFIRST(rbr)           rb_first(rbr)
#define LTRLAST(rbr)            rb_last(rbr)

#define LTILNK(rbn)             (((LTI *) (rbn))->lnk)
#define LTINAME(rbn)            (((LTI *) (rbn))->name)
#define LTICLL(rbn)             (((LTI *) (rbn))->cll)

#define LTILEFT(rbn)            ((LTI *) ((&LTILNK(rbn))->rb_left))
#define LTIRIGHT(rbn)           ((LTI *) ((&LTILNK(rbn))->rb_right))
#define LTIPARENT(rbn)          ((LTI *) rb_parent(&LTILNK(rbn)))
#define LTINEXT(rbn)            ((LTI *) rb_next(&LTILNK(rbn)))
#define LTIPREV(rbn)            ((LTI *) rb_prev(&LTILNK(rbn)))

#define LTVLNK(ltv)             ((ltv)->lnk)
#define LTVFLAGS(ltv)           ((ltv)->flags)
#define LTVDATA(ltv)            ((ltv)->data)
#define LTVLEN(ltv)             ((ltv)->len)
#define LTVREFS(ltv)            ((ltv)->refs)
#define LTVSUBS(ltv)            ((ltv)->subs)

extern LTI *lti_new(char *name);
extern LTV *ltv_new(char *data);
extern LTI *lt_get(LTR *ltr,char *name,int insert);
extern void *lt_traverse(LTR *ltr,LT_OP op,void *data);
extern void *lt_dump(LTR *ltr,void *data);

#endif

