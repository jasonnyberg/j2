#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

FILE *dumpfile;

#if 0
digraph iftree
{
compound=true
	ordering=out concentrate=true
	node [shape=record]
edge []
subgraph clusterA { label=aaa 111 subgraph clusterB }
subgraph clusterB { color=red label=bbb 2 3 4 }
subgraph clusterC { color=white xxx label=ccc 333 444 }

 subgraph clusterC ->  subgraph clusterA
444 -> 2 [lhead=clusterB]
a [shape=record label="<f1>a|<f2>b|<f3>c"]
a:f1 -> a:f2
a:f3 -> 2 [lhead=clusterA]
}
#endif

/* temporary until reflect works */
void *CLL_dump(CLL *cll,void *data);
void *RBN_dump(RBN *rbn,void *data);
void *LTV_dump(LTV *ltv);

void *LTV_dump(LTV *ltv)
{
    if (ltv->len)
    {
        fprintf(dumpfile,"%d [style=filled shape=box label=\"",ltv);
        fstrnprint(dumpfile,ltv->data,ltv->len);
        fprintf(dumpfile,"\"]\n");
    }
    else
    {
        fprintf(dumpfile,"%d [label=\"\" shape=box style=filled height=.1 width=.1]\n",ltv);
    }
    if (ltv->rbr.rb_node)
    {
        fprintf(dumpfile,"%d -> %d\n",ltv,ltv->rbr.rb_node);
        RBR_traverse(&ltv->rbr,RBN_dump,&ltv->rbr);
    }
    return NULL;
}

void *RBN_dump(RBN *rbn,void *data)
{
    LTI *lti=(LTI *) rbn; // each RBN is really an LTI
    CLL *lnk;
    //fprintf(dumpfile,"subgraph clusterRBN%d { %d }\n",data,lti);
    fprintf(dumpfile,"%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
    if (rb_parent(rbn))
        fprintf(dumpfile,"%d -> %d\n",rb_parent(rbn),rbn);
    if (lnk=CLL_get(&lti->cll,0,0))
    {
        fprintf(dumpfile,"%d -> %d\n",rbn,lnk);
        CLL_traverse(&lti->cll,0,CLL_dump,&lti->cll);
    }
    return NULL;
}

void *CLL_dump(CLL *cll,void *data)
{
    LTVR *ltvr=(LTVR *) cll; // each CLL node is really an LTVR
    fprintf(dumpfile,"%d [label=\"\" shape=point]\n",cll);
    if (ltvr->ltv) fprintf(dumpfile,"%d -> %d\n",cll,ltvr->ltv);
    if (cll->lnk[1]!=data)
        fprintf(dumpfile,"%d -> %d\n",cll->lnk[1],cll);
    LTV_dump(ltvr->ltv);
    return NULL;
}


extern int Gmymalloc;

int edict_dump(EDICT *edict)
{
    int status;
    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\n\tordering=out concentrate=true\n\tnode [shape=record]\n\tedge []\n");
    //fprintf(dumpfile,"root=\"%d\"\n",-1);
    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"%d [label=dict]\n",&edict->dict);
    TRY(CLL_traverse(&edict->dict,0,CLL_dump,NULL),0,finish,"\n");
    //TRYLOG(CLL_dump(&edict->code,(void *) -1),0,done,"\n");
    //GVGRP((void *) -1,&edict->code);
 finish:
    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

 done:
    return status;
}


EDICT edict;

int main()
{
    LTI *lti;
    LTV *root=LTV_new("ROOT",-1,0);
    edict_init(&edict,root);
    edict_repl(&edict);
    edict_destroy(&edict);
}
