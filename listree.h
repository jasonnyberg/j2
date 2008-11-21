#ifndef LISTREE_H
#define LISTREE_H

//////////////////////////////////////////////////
// Circular list (StaQ) (sentinel implementation)
//////////////////////////////////////////////////

// head=lnk[0],tail=lnk[1]
struct CLL { struct CLL *lnk[2]; } __attribute__((aligned(sizeof(long))));

typedef struct CLL CLL;
typedef void *(*CLL_OP)(CLL *lnk,void *data);

extern CLL *CLL_init(CLL *lst);
extern void CLL_destroy(CLL *lst);

extern CLL *CLL_put(CLL *lst,CLL *lnk,int end);
extern CLL *CLL_pop(CLL *lnk);
extern CLL *CLL_get(CLL *lst,int pop,int end);

extern void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data);


//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

#include "rbtree.h"

#define RBR struct rb_root
#define RBN struct rb_node

typedef enum { LT_DUP=1<<0, LT_RO=1<<1, LT_CTYPE=1<<2 } LTV_FLAGS;

typedef struct
{
    CLL lnk;
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
    RBR subs;
} LTV; // LisTree Value

typedef struct
{
    union { RBN rbn; CLL cll; } lnk;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*LT_OP)(RBN *ltn,void *data);

#define RBRROOT(rbr)   ((rbr)->rb_node)
#define RBRFIRST(rbr)  rb_first(rbr)
#define RBRLAST(rbr)   rb_last(rbr)
        

#define LTILEFT(rbn)   ((LTI *) ((RBN *) (rbn))->rb_left)
#define LTIRIGHT(rbn)  ((LTI *) ((RBN *) (rbn))->rb_right)
#define LTIPARENT(rbn) ((LTI *) rb_parent((RBN *) (rbn)))
#define LTINEXT(rbn)   ((LTI *) rb_next((RBN *) (rbn)))
#define LTIPREV(rbn)   ((LTI *) rb_prev((RBN *) (rbn)))

extern LTV *ltv_new(char *data);
extern void ltv_free(CLL *cll);

extern LTI *lti_new(char *name);
extern void lti_free(RBN *rbn);

extern LTV *lti_assign(LTI *lti,LTV *ltv)

extern LTI *lt_lookup(RBR *rbr,char *name,int insert);
extern void *lt_traverse(RBR *rbr,LT_OP op,void *data);



//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////





#endif

