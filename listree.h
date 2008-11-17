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
    CLL lst;
} LTI; // LisTreeItem

//#define LTRROOT(ltr)            ((LTI *) ((ltr)->rb_node))
#define LTRROOT(ltr)            (((ltr)->rb_node))
#define LTRFIRST(ltr)           ((LTI *) (rb_first(((LTR *)(ltr)))))
#define LTRLAST(ltr)            ((LTI *) (rb_last(((LTR *)(ltr)))))

#define LTILNK(lti)             (((LTI *) (lti))->lnk)
#define LTINAME(lti)            (((LTI *) (lti))->name)
#define LTILST(lti)             (((LTI *) (lti))->lst)

#define LTILEFT(lti)            ((LTI *) (((LTI *) (&LTILNK(lti))->rb_left)))
#define LTIRIGHT(lti)           ((LTI *) (((LTI *) (&LTILNK(lti))->rb_right)))
#define LTIPARENT(lti)          ((LTI *) ((LTI *) (rb_parent(&LTILNK(lti)))))
#define LTINEXT(lti)            ((LTI *) ((LTI *) (rb_next(&LTILNK(lti)))))
#define LTIPREV(lti)            ((LTI *) ((LTI *) (rb_prev(&LTILNK(lti)))))

#define LTVLNK(ltv)             (((LTV *) (ltv))->lnk)
#define LTVFLAGS(ltv)           (((LTV *) (ltv))->flags)
#define LTVDATA(ltv)            (((LTV *) (ltv))->data)
#define LTVLEN(ltv)             (((LTV *) (ltv))->len)
#define LTVREFS(ltv)            (((LTV *) (ltv))->refs)
#define LTVSUBS(ltv)            (((LTV *) (ltv))->subs)

extern LTI *lt_get(LTR *ltr,char *name,int insert);

#endif

