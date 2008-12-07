#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

/* temporary until reflect works */
void *CLL_dump(CLL *cll,void *data);
void *RBR_dump(RBR *rbr,void *data);
void *RBN_dump(RBN *rbn,void *data);
void *LTVR_dump(CLL *lnk,void *data);
void *LTV_dump(LTV *ltv,void *data);


void *LTV_dump(LTV *ltv,void *data)
{
    printf("%s:%s\n",(char *) data,ltv->data);
    RBR_dump(&ltv->rbr,data);
    return NULL;
}

void *LTVR_dump(CLL *lnk,void *data)
{
    LTV_dump(((LTVR *) lnk)->ltv,data);
    return NULL;
}

void *RBN_dump(RBN *rbn,void *data)
{
    char *prefix=alloca(strlen(data)+strlen(((LTI *) rbn)->name)+1);
    sprintf(prefix,"%s.%s",(char *) data,((LTI *) rbn)->name);
    return CLL_traverse(&((LTI *) rbn)->cll,0,LTVR_dump,prefix);
}

void *RBR_dump(RBR *rbr,void *data)
{
    return RBR_traverse(rbr,RBN_dump,data);
}

void *CLL_dump(CLL *cll,void *data)
{
    return CLL_traverse(cll,0,LTVR_dump,data);
}

void edict_dump(EDICT *edict)
{
    printf("---------------------\n");
    CLL_dump(&edict->anons,"anons:");
    CLL_dump(&edict->stack,"stack:");
    printf("---------------------\n");
}

EDICT edict;

int jj_test()
{
    edict_add(&edict,LTV_new("123",-1,0));
    edict_add(&edict,LTV_new("456",-1,0));
    edict_add(&edict,LTV_new("789",-1,0));
    edict_add(&edict,LTV_new("abc",-1,0));

    edict_name(&edict,"name1",-1,0);
    edict_name(&edict,"name2a.name2b",-1,0);
    edict_name(&edict,"name3a,name3b",-1,0);
    edict_name(&edict,"name4",-1,0);

    edict_dump(&edict);
}

int main()
{
    LTI *lti;
    edict_init(&edict);
    jj_test();
    jj_test();
    edict_destroy(&edict);
}

