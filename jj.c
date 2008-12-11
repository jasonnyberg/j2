#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

#define GVITEM(type,id,label) printf("\t\"%x\" [label=\"" type "%s\"];\n",id,label)
#define GVEDGE(id,child) printf("\t\"%x\" -> \"%x\"\n",id,child)

/* temporary until reflect works */
void *CLL_dump(CLL *cll,void *data);
void *RBR_dump(RBR *rbr,void *data);
void *RBN_dump(RBN *rbn,void *data);
void *LTVR_dump(CLL *lnk,void *data);
void *LTV_dump(LTV *ltv,void *data);

int terse=1;

void *LTV_dump(LTV *ltv,void *data)
{
    GVITEM("LTV",ltv,"");
    GVEDGE(data,ltv);
    
    GVITEM("",ltv->data,ltv->data);
    GVEDGE(ltv,ltv->data);
    
    if (!terse) RBR_dump(&ltv->rbr,ltv);
    else RBR_traverse(&ltv->rbr,RBN_dump,ltv);
    return NULL;
}

void *LTVR_dump(CLL *ltvr,void *data)
{
    GVITEM("LTVR",ltvr,"");
    GVEDGE(data,ltvr);
    if (!terse) LTV_dump(((LTVR *) ltvr)->ltv,ltvr);
    else
    {
        LTV *ltv=((LTVR *) ltvr)->ltv;
        GVITEM("",ltv->data,ltv->data);
        GVEDGE(ltvr,ltv->data);
        RBR_traverse(&ltv->rbr,RBN_dump,ltvr);
    }
    return NULL;
}

void *RBN_dump(RBN *rbn,void *data)
{
    GVITEM("",rbn,((LTI *) rbn)->name);
    GVEDGE(data,rbn);
    if (!terse) CLL_dump(&((LTI *) rbn)->cll,rbn);
    else CLL_traverse(&((LTI *) rbn)->cll,0,LTVR_dump,rbn);
    return NULL;
}

void *RBR_dump(RBR *rbr,void *data)
{
    if (rbr->rb_node)
    {
        GVITEM("RBR",rbr,"");
        GVEDGE(data,rbr);
        RBR_traverse(rbr,RBN_dump,rbr);
    }
    return NULL;
}

void *CLL_dump(CLL *cll,void *data)
{
    GVITEM("CLL",cll,"");
    GVEDGE(data,cll);
    CLL_traverse(cll,0,LTVR_dump,cll);
    return NULL;
}

void edict_dump(EDICT *edict)
{
    printf("digraph iftree\n{\n\tgraph [concentrate=true]; node [shape=record]; edge [];\n");
    GVITEM("EDICT",NULL,"");
    printf("root=\"%s\"\n",NULL);
    CLL_dump(&edict->anons,NULL);
    CLL_dump(&edict->stack,NULL);
    printf("}\n");
}



EDICT edict;

int jj_test()
{
    edict_add(&edict,LTV_new("123",-1,0));
    edict_add(&edict,LTV_new("456",-1,0));
    edict_add(&edict,LTV_new("789",-1,0));
    edict_add(&edict,LTV_new("abc",-1,0));
    edict_add(&edict,LTV_new("xyz",-1,0));

    edict_name(&edict,"name1",-1,0);
    edict_name(&edict,"name2a.name2b",-1,0);
    edict_name(&edict,"name3a,name3b",-1,0);
    edict_name(&edict,"name4",-1,0);
    
    edict_ref(&edict,"name2a.name2b",-1,0,0);
    edict_ref(&edict,"name2a.name2b",-1,0,0);
    edict_ref(&edict,"name2a.name2b",-1,0,0);
    edict_ref(&edict,"name2a.name2b",-1,0,0);
    edict_name(&edict,"name5",-1,0);
    edict_name(&edict,"name6",-1,0);
}

int main()
{
    LTI *lti;
    edict_init(&edict);
    jj_test();
    edict_dump(&edict);
    jj_test();
    edict_dump(&edict);
    edict_destroy(&edict);
}

