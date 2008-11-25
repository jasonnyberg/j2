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
    }
}

void edict_destroy(EDICT *edict)
{
    if (edict)
    {
        RBR_release(&edict->root,LTI_release);
        CLL_release(&edict->anons,LTVR_release);
        CLL_release(&edict->stack,LTVR_release);
        
        DELETE(edict);
    }
}


LTV *edict_assign(LTI *lti,LTV *ltv,int end)
{
}

