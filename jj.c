#include <stdio.h>
#include "edict.h"

/* temporary until reflect works */
void *LT_dump_ltvr(CLL *lnk,void *data)
{
    printf("  %s%s\n",(char *) data,((LTVR *) lnk)->ltv->data);
    return NULL;
}

void *LT_dump_rbn(RBN *rbn,void *data)
{
    printf("%s%s:\n",(char *) data,((LTI *) rbn)->name);
    return CLL_traverse(&((LTI *) rbn)->cll,0,LT_dump_ltvr,data);
}

void *lt_dump(RBR *rbr,void *data)
{
    printf("***********************\n");
    return RBR_traverse(rbr,LT_dump_rbn,data);
}



EDICT edict;

int main()
{
    LTI *lti;
    edict_init(&edict);
    
    lti=LT_lookup(&edict.root,"aaa",1);
    LTV_put(&edict.ltvrtrash,&lti->cll,LTV_new("123",0,LT_STR|LT_DUP),0);
    lt_dump(&edict.root,"");
    
    lti=LT_lookup(&edict.root,"bbb",1);
    LTV_put(&edict.ltvrtrash,&lti->cll,LTV_new("456",0,LT_STR|LT_DUP),0);
    lt_dump(&edict.root,"");
    
    lti=LT_lookup(&edict.root,"aaa",1);
    LTV_put(&edict.ltvrtrash,&lti->cll,LTV_new("789",0,LT_STR|LT_DUP),0);
    lt_dump(&edict.root,"");
    
    lti=LT_lookup(&edict.root,"aaa",1);
    LTV_put(&edict.ltvrtrash,&lti->cll,LTV_new("abc",0,LT_STR|LT_DUP),1);
    lt_dump(&edict.root,"");
}

