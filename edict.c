#include "util.h"
#include "edict.h"


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

EDICT *edict_init(EDICT *edict)
{
    if (edict)
    {
        RBR_init(&edict->root);
        CLL_init(&edict->anons);
        CLL_init(&edict->stack);
        RBR_init(&edict->ltitrash);
        CLL_init(&edict->ltvrtrash);
        CLL_init(&edict->ltvtrash);
    }
}

void edict_destroy(EDICT *edict)
{
    if (edict)
    {
        RBR_release(&edict->root,LTI_free,NULL);
        CLL_release(&edict->anons,LTVR_free,NULL);
        CLL_release(&edict->stack,LTVR_free,NULL);
        RBR_release(&edict->ltitrash,LTI_free,NULL);
        CLL_release(&edict->ltvrtrash,LTVR_free,NULL);
        CLL_release(&edict->ltvtrash,LTVR_free,NULL);
        
        DELETE(edict);
    }
}


