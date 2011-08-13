//////////////////////////////////////////////////
// Circular list (StaQ) (sentinel implementation)
//////////////////////////////////////////////////

#ifndef CLL_H
#define CLL_H

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

#endif
