#include "listree.h"

typedef struct
{
    RBR root;
    CLL anons;
    CLL stack;
    RBR ltitrash;
    CLL ltvrtrash;
    CLL ltvtrash; // pop an ltvr from ltvrtrash to link an ltv
} EDICT;

//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

extern EDICT *edict_init();
extern void edict_destroy(EDICT *edict);

extern LTV *edict_assign(LTI *lti,LTV *ltv,int end);
