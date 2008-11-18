#ifndef LISTREE_H
#define LISTREE_H

//////////////////////////////////////////////////
// Circular list (StaQ) (sentinel implementation)
//////////////////////////////////////////////////

// head=next[0],tail=next[1]
typedef struct CLL { struct CLL *lnk[2]; } CLL;
typedef CLL *(*CLL_OP)(CLL *lnk,void *data);

extern CLL *CLL_init(CLL *lst);
extern CLL *CLL_put(CLL *lst,CLL *lnk,int tail);
extern CLL *CLL_get(CLL *lst,CLL *lnk,int tail,int pop);
extern CLL *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data);


//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

#include "rbtree.h"

typedef struct
{
    CLL lnk;
    int flags;
    void *data;
    int len;
    int refs;
    struct rb_root subs;
} LTV; // LisTree Value

typedef struct
{
    struct rb_node lnk;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

#define LTRROOT(rbr)            ((rbr)->rb_node)
#define LTRFIRST(rbr)           rb_first(rbr)
#define LTRLAST(rbr)            rb_last(rbr)

#define LTILNK(rbn)             (((LTI *) (rbn))->lnk)
#define LTINAME(rbn)            (((LTI *) (rbn))->name)
#define LTILST(rbn)             (((LTI *) (rbn))->cll)

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

extern LTI *lt_get(struct rb_root *ltr,char *name,int insert);

#endif

