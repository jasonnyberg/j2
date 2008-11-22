#include "edict.h"


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

EDICT_ENV *edict_init()
{
    EDICT *edict=NEW(EDICT);
    if (edict)
    {
        RBR_init(edict->root);
        CLL_init(edict->anons);
        CLL_init(edict->stack);
        RBR_init(edict->ltitrash);
        CLL_init(edict->ltvrtrash);
        CLL_init(edict->ltvtrash);
    }
}

void edict_destroy(EDICT *edict)
{
    if (edict)
    {
        RBR_destroy(edict->root);
        CLL_destroy(edict->anons);
        CLL_destroy(edict->stack);
        RBR_destroy(edict->ltitrash);
        CLL_destroy(edict->ltvrtrash);
        CLL_destroy(edict->ltvtrash);
        
        DELETE(edict);
    }
}


