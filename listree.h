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
extern CLL *CLL_splice(CLL *dst,CLL *src,int end);
extern CLL *CLL_pop(CLL *lnk);
extern CLL *CLL_get(CLL *lst,int pop,int end);
extern CLL *CLL_find(CLL *lst,void *data,int len);

extern void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data);

#define CLL_EMPTY(lst) (!CLL_get((lst),0,0))

//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

extern int ltv_count,ltvr_count,lti_count;

#include "rbtree.h"

#define RBR struct rb_root
#define RBN struct rb_node

typedef enum {
    LT_DUP=1<<0, // bufdup data for new LTV
        LT_DEL=1<<1, // free not-referenced LTV data upon release
        LT_RO=1<<2, // never release LTV/children
        LT_FILE=1<<3, // LTV data is a FILE *
        LT_CVAR=1<<4, // LTV data is a C variable
        LT_AVIS=1<<5, // absolute traversal visitation flag
        LT_RVIS=1<<6 // recursive traversal visitation flag
} LTV_FLAGS;

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
    void *metadata;
} LTVR; // LisTree Value Reference

typedef struct
{
    CLL repo[0]; // union without union semantics
    RBN rbn;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*RB_OP)(RBN *rbn,void *data);

extern RBR *RBR_init(RBR *rbr);
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn));
extern void *RBR_traverse(RBR *rbr,RB_OP op,void *data);

extern LTV *LTV_new(void *data,int len,LTV_FLAGS flags);
extern void LTV_free(LTV *ltv);

extern LTVR *LTVR_new(void *metadata);
extern void *LTVR_free(LTVR *ltvr);

extern LTI *LTI_new(char *name,int len);
extern void LTI_free(LTI *lti);

extern LTI *LT_find(RBR *rbr,char *name,int len,int insert);

//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

typedef void *(*LTOBJ_OP)(LTVR *ltvr,LTI *lti,LTV *ltv,void *data);
struct LTOBJ_DATA { LTOBJ_OP preop; LTOBJ_OP postop; int depth; void *data; int halt:1; int add:1; int rem:1; int luf:1; };

void *LTV_traverse(LTV *ltv,char *pat,unsigned len,void *data);
void *LTVR_traverse(CLL *cll,char *pat,unsigned len,void *data);
void *LTI_traverse(RBN *rbn,char *pat,unsigned len,void *data);

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////
extern void LTV_release(LTV *ltv);
extern void LTVR_release(CLL *cll);
extern void LTI_release(RBN *rbn);

//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

extern LTV *LTV_put(CLL *cll,LTV *ltv,int end,void *metadata);
extern LTV *LTV_get(CLL *cll,int pop,int end,void *match,int matchlen,void **metadata);

extern void LT_init();

#endif

