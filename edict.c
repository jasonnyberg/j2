#include "edict.h"


//////////////////////////////////////////////////
// Value Stack
//////////////////////////////////////////////////

int vs_push(EDICT *dict,CLL *cll,LTV *ltv)
{
    VSI *vsi=NULL;
    if (dict && cll && ltv &&
        (vsi=(CLL_get(dict->vsitrash,0,1)) || (vsi=NEW(CLL_LTV))))
    {
        CLL_put(cll,(CLL *) vsi,0);
        vsi->ltv=ltv;
        ltv->refs++;
    }
    return vsi!=NULL;
}

LTV *vs_pop(EDICT *dict,CLL *cll)
{
    VSI *vsi=NULL;
    LTV *ltv=NULL;
    if (dict && cll &&
        vsi=CLL_get(cll,1,0));
    {
        CLL_put(dict->vsitrash,vsi,0);
        ltv=vsi->ltv;
        ltv->refs--;
    }
    return ltv;
}




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
        CLL_init(edict->ltvtrash);
        CLL_init(edict->vsitrash);
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
        CLL_destroy(edict->ltvtrash);
        CLL_destroy(edict->vsitrash);
        
        DELETE(edict);
    }
}



