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
extern void CLL_release(CLL *lst,void (*cll_release)(CLL *cll));


extern CLL *CLL_put(CLL *lst,CLL *lnk,int end);
extern CLL *CLL_pop(CLL *lnk);
extern CLL *CLL_get(CLL *lst,int pop,int end);

extern void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data);

//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

extern CLL ltv_repo,ltvr_repo,lti_repo;

#include "rbtree.h"

#define RBR struct rb_root
#define RBN struct rb_node

typedef enum { LT_DUP=1<<0, LT_RO=1<<1, LT_CVAR=1<<2, } LTV_FLAGS;

typedef struct
{
    CLL repo[0]; // union without union semantics
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
    RBR rbr;
} LTV; // LisTree Value

typedef struct
{
    CLL repo[0]; // union without union semantics
    CLL cll;
    LTV *ltv;
} LTVR;

typedef struct
{
    CLL repo[0]; // union without union semantics
    RBN rbn;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*LT_OP)(RBN *ltn,void *data);

extern RBR *RBR_init(RBR *rbr);
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn));
extern void *RBR_traverse(RBR *rbr,LT_OP op,void *data);

extern LTV *LTV_new(void *data,int len,int flags);
extern void LTV_free(LTV *ltv);

extern LTVR *LTVR_new();
extern void LTVR_free(LTVR *ltvr);

extern LTI *LTI_new(char *name);
extern void LTI_free(LTI *lti);

extern LTI *LT_lookup(RBR *rbr,char *name,int len,int insert);

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////
extern void LTV_release(LTV *ltv);
extern void LTVR_release(CLL *cll);
extern void LTI_release(RBN *rbn);

//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

extern LTV *LTV_put(CLL *cll,LTV *ltv,int end);
extern LTV *LTV_get(CLL *cll,int pop,int end);

extern void LT_init();
extern LTV *LT_put(RBR *rbr,LTV *ltv,char *name,int len,int end);
extern LTV *LT_get(RBR *rbr,char *name,int len,int pop,int end);


#endif

