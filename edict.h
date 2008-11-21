#include "listree.h"

typedef struct
{
    RBR root;
    CLL anons;
    CLL stack;
    RBR ltitrash;
    CLL ltvtrash;
    CLL vsitrash;
} EDICT;


//////////////////////////////////////////////////
// Value Stack
//////////////////////////////////////////////////

typedef struct { CLL lnk; LTV *ltv; } CLL_LTV;
extern void vs_push(VSI *vsi,LTV *ltv);
extern LTV *vs_pop(VSI *vsi);


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

extern EDICT_ENV *edict_init();
extern void edict_destroy(EDICT_ENV *edict);





