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
extern void CLL_release(CLL *lst,void (*cll_release)(CLL *cll,void *data),void *data);


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

typedef enum { LT_STR=1<<0, LT_DUP=1<<1, LT_RO=1<<2, LT_CTYPE=1<<3 } LTV_FLAGS;

typedef struct
{
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
    RBR subs;
} LTV; // LisTree Value

typedef struct { CLL cll; LTV *ltv; } LTVR;

typedef struct
{
    union { RBN rbn; CLL cll; } u;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*LT_OP)(RBN *ltn,void *data);

extern RBR *RBR_init(RBR *rbr);
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn,void *data),void *data);
extern void *RBR_traverse(RBR *rbr,LT_OP op,void *data);

extern LTV *LTV_new(void *data,int len,int flags);
extern LTV *LTV_put(CLL *trash,CLL *cll,LTV *ltv,int end);
extern LTV *LTV_get(CLL *trash,CLL *cll,int pop,int end);

extern LTI *LTI_new(char *name);

extern LTI *LT_lookup(RBR *rbr,char *name,int insert);


extern void LTVR_free(CLL *cll,void *data);
extern void LTI_free(RBN *rbn,void *data);


//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////





#endif

