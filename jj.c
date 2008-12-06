#include <stdio.h>
#include "util.h"
#include "edict.h"

/* temporary until reflect works */
void *LTV_dump(LTV *ltv,void *data)
{
    printf("  %s%s\n",(char *) data,ltv->data);
    return NULL;
}

void *LTVR_dump(CLL *lnk,void *data)
{
    LTV_dump(((LTVR *) lnk)->ltv,data);
    return NULL;
}

void *RBN_dump(RBN *rbn,void *data)
{
    printf("%s%s:\n",(char *) data,((LTI *) rbn)->name);
    return CLL_traverse(&((LTI *) rbn)->cll,0,LTVR_dump,data);
}

void *RBR_dump(RBR *rbr,void *data)
{
    return RBR_traverse(rbr,RBN_dump,data);
}

void *CLL_dump(CLL *cll,void *data)
{
    return CLL_traverse(cll,0,LTVR_dump,data);
}


EDICT edict;

int jj_test()
{
    edict_add(&edict,LTV_new("123",0,LT_STRDUP));
    edict_add(&edict,LTV_new("456",0,LT_STRDUP));
    edict_add(&edict,LTV_new("789",0,LT_STRDUP));
    edict_add(&edict,LTV_new("abc",0,LT_STRDUP));

    printf("---------------------\n");
    CLL_dump(&edict.anons,"anons: ");
    printf("---------------------\n");
    CLL_dump(&edict.stack,"stack: ");
    printf("---------------------\n");
    
    printf("***********************\n");
}

int main()
{
    LTI *lti;
    edict_init(&edict);
    jj_test();
    jj_test();
    edict_destroy(&edict);
}

