//////////////////////////////////////////////////
// Circular list (StaQ) (sentinel implementation)
//////////////////////////////////////////////////

#ifndef CLL_H
#define CLL_H

// head=lnk[0],tail=lnk[1]
struct CLL { struct CLL *lnk[2]; } __attribute__((aligned(sizeof(long))));

typedef struct CLL CLL;
typedef void *(*CLL_OP)(CLL *lnk,void *data);

enum { HEAD=0,TAIL=1,FWD=0,REV=1,KEEP=0,POP=1 };

extern CLL *CLL_init(CLL *lst);
extern void CLL_release(CLL *lst,void (*cll_release)(CLL *cll));


extern CLL *CLL_put(CLL *lst,CLL *lnk,int end);
extern CLL *CLL_splice(CLL *dst,int end,CLL *src);
extern CLL *CLL_pop(CLL *lnk);
extern CLL *CLL_get(CLL *lst,int pop,int end);
extern CLL *CLL_find(CLL *lst,void *data,int len);

extern void *CLL_traverse(CLL *lst,int end,CLL_OP op,void *data);

#define CLL_EMPTY(lst) (!CLL_get((lst),0,0))

#endif
