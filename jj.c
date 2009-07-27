#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

FILE *dumpfile;

#define GVITEM(type,id,label,len,props) fprintf(dumpfile,"\t\"%x\" [label=\"" type,id),fstrnprint(dumpfile,label,len),fprintf(dumpfile,"\"] %s;\n",props)
#define GVEDGE(parent,child,props) fprintf(dumpfile,"\t\"%x\" -> \"%x\" %s\n",parent,child,props)
#define GVGRP(parent,child) fprintf(dumpfile,"\tsubgraph \"cluster%x\" { \"%x\" }\n",parent,child)

/* temporary until reflect works */
void *CLL_dump(CLL *cll,void *data);
void *RBR_dump(RBR *rbr,void *data);
void *RBN_dump(RBN *rbn,void *data);
void *LTVR_dump(CLL *lnk,void *data);
void *LTV_dump(LTV *ltv,void *data);

void *LTV_dump(LTV *ltv,void *data)
{
    GVITEM("LTV",ltv,"",0,"");
    GVEDGE(data,ltv,"");
    GVITEM("",ltv->data,ltv->data,ltv->len,"[shape=ellipse]");
    GVEDGE(ltv,ltv->data,"");
    RBR_dump(&ltv->rbr,ltv);
    return NULL;
}

void *LTVR_dump(CLL *ltvr,void *data)
{
    GVITEM("LTVR",ltvr,"",0,"");
    GVEDGE(ltvr,ltvr->lnk[0],"[color=green]");
    LTV_dump(((LTVR *) ltvr)->ltv,ltvr);
    GVGRP(data,ltvr);
    return NULL;
}

void *RBN_dump(RBN *rbn,void *data)
{
    GVITEM("",rbn,((LTI *) rbn)->name,-1,"");
    if (rb_parent(rbn)) GVEDGE(rb_parent(rbn),rbn,"[color=blue]");
    CLL_dump(&((LTI *) rbn)->cll,rbn);
    GVGRP(data,rbn);
    GVGRP(rbn,&((LTI *) rbn)->cll);
    return NULL;
}

void *RBR_dump(RBR *rbr,void *data)
{
    if (rbr->rb_node)
    {
        GVITEM("RBR",rbr,"",0,"");
        GVEDGE(data,rbr,"");
        GVEDGE(rbr,rbr->rb_node,"[color=red]");
        GVGRP(rbr,rbr);
        RBR_traverse(rbr,RBN_dump,rbr);
    }
    return NULL;
}

void *CLL_dump(CLL *cll,void *data)
{
    if (cll->lnk[0]!=cll)
    {
        GVITEM("CLL",cll,"",0,"");
        GVEDGE(data,cll,"[color=green]");
        GVEDGE(cll,cll->lnk[0],"[color=green]");
        GVGRP(cll,cll);
        CLL_traverse(cll,0,LTVR_dump,cll);
    }
    return NULL;
}

int edict_dump(EDICT *edict)
{
    int status;
    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\n\tordering=out concentrate=true\n\tnode [shape=record]\n\tedge []\n");
    GVITEM("EDICT",-1,"",0,"");
    fprintf(dumpfile,"root=\"%x\"\n",-1);
    TRY(CLL_dump(&edict->anon,(void *) -1),0,done,"\n");
    TRY(CLL_dump(&edict->dict,(void *) -1),0,done,"\n");
    //TRYLOG(CLL_dump(&edict->code,(void *) -1),0,done,"\n");
    GVGRP((void *) -1,&edict->anon);
    //GVGRP((void *) -1,&edict->code);
    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

 done:
    return status;
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
    //jj_test();
    //jj_test();
    //edict_dump(&edict);
    edict_repl(&edict);
    edict_destroy(&edict);
}
