#include "listree.h"

typedef struct
{
    RBR root;
    CLL anons;
    CLL stack;
    RBR ltitrash;
    CLL ltvrtrash;
    CLL ltvtrash; // ltvr + ltv
} EDICT;

//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

extern EDICT_ENV *edict_init();
extern void edict_destroy(EDICT_ENV *edict);

extern LTV *edict_assign(LTI *lti,LTV *ltv,int end);
