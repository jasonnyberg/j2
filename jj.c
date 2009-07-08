#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

#define GVITEM(type,id,label,props) printf("\t\"%x\" [label=\"" type "%s\"] %s;\n",id,label,props)
#define GVEDGE(parent,child,props) printf("\t\"%x\" -> \"%x\" %s\n",parent,child,props)
#define GVGRP(parent,child) printf("\tsubgraph \"cluster%x\" { \"%x\" }\n",parent,child)

/* temporary until reflect works */
void *CLL_dump(CLL *cll,void *data);
void *RBR_dump(RBR *rbr,void *data);
void *RBN_dump(RBN *rbn,void *data);
void *LTVR_dump(CLL *lnk,void *data);
void *LTV_dump(LTV *ltv,void *data);

void *LTV_dump(LTV *ltv,void *data)
{
    GVITEM("LTV",ltv,"","");
    GVEDGE(data,ltv,"");
    
    GVITEM("",ltv->data,ltv->data,"[shape=ellipse]");
    GVEDGE(ltv,ltv->data,"");
    
    RBR_dump(&ltv->rbr,ltv);
    return NULL;
}

void *LTVR_dump(CLL *ltvr,void *data)
{
    GVITEM("LTVR",ltvr,"","");
    GVEDGE(ltvr,ltvr->lnk[0],"");
    LTV_dump(((LTVR *) ltvr)->ltv,ltvr);
    GVGRP(data,ltvr);
    return NULL;
}

void *RBN_dump(RBN *rbn,void *data)
{
    GVITEM("",rbn,((LTI *) rbn)->name,"");
    if (rb_parent(rbn)) GVEDGE(rb_parent(rbn),rbn,"");
    CLL_dump(&((LTI *) rbn)->cll,rbn);
    GVGRP(data,rbn);
    GVGRP(rbn,&((LTI *) rbn)->cll);
    return NULL;
}

void *RBR_dump(RBR *rbr,void *data)
{
    if (rbr->rb_node)
    {
        GVITEM("RBR",rbr,"","");
        GVEDGE(data,rbr,"");
        GVEDGE(rbr,rbr->rb_node,"");
        GVGRP(rbr,rbr);
        RBR_traverse(rbr,RBN_dump,rbr);
    }
    return NULL;
}

void *CLL_dump(CLL *cll,void *data)
{
    if (cll->lnk[0]!=cll)
    {
        GVITEM("CLL",cll,"","");
        GVEDGE(data,cll,"");
        GVEDGE(cll,cll->lnk[0],"");
        GVGRP(cll,cll);
        CLL_traverse(cll,0,LTVR_dump,cll);
    }
    return NULL;
}

void edict_dump(EDICT *edict)
{
    printf("digraph iftree\n{\n\tordering=out concentrate=true\n\tnode [shape=record]\n\tedge []\n");
    GVITEM("EDICT",-1,"","");
    printf("root=\"%x\"\n",-1);
    CLL_dump(&edict->anon,(void *) -1);
    CLL_dump(&edict->code,(void *) -1);
    GVGRP((void *) -1,&edict->anon);
    GVGRP((void *) -1,&edict->code);
    printf("}\n");
}



EDICT edict;

int jj_test()
{
    edict_add(&edict,LTV_new("123",-1,0),NULL);
    edict_add(&edict,LTV_new("456",-1,0),NULL);
    edict_add(&edict,LTV_new("789",-1,0),NULL);
    edict_add(&edict,LTV_new("abc",-1,0),NULL);
    edict_add(&edict,LTV_new("xyz",-1,0),NULL);

    edict_name(&edict,"name1",-1,0,NULL);
    edict_name(&edict,"name2a.name2b",-1,0,NULL);
    edict_name(&edict,"name3a,name3b",-1,0,NULL);
    edict_name(&edict,"name4",-1,0,NULL);
    
    edict_ref(&edict,"name2a.name2b",-1,0,0,NULL);
    edict_ref(&edict,"name2a.name2b",-1,0,0,NULL);
    edict_ref(&edict,"name2a.name2b",-1,0,0,NULL);
    edict_ref(&edict,"name2a.name2b",-1,0,0,NULL);
    edict_name(&edict,"name5",-1,0,NULL);
    edict_name(&edict,"name6",-1,0,NULL);
}

int main()
{
    LTI *lti;
    LTV *root=LTV_new("ROOT",-1,0);
    edict_init(&edict,root);
    jj_test();
    jj_test();
    //edict_dump(&edict);
    edict_thread(&edict);
    edict_destroy(&edict);
}
