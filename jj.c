#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

FILE *dumpfile;

void *cluster_ltv(LTV *ltv,void *data)
{
    void *cluster_rbn(RBN *rbn,void *data) // define rbn objs
    {
        LTI *lti=(LTI *) rbn;
        fprintf(dumpfile,"\t%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        return NULL;
    }
    void *cluster_rbn2(RBN *rbn,void *data) // define rbn links
    {
        LTI *lti=(LTI *) rbn;
        if (rb_parent(&lti->rbn))
            fprintf(dumpfile,"\t%d -> %d\n",rb_parent(&lti->rbn),&lti->rbn);
        return NULL;
    }
    fprintf(dumpfile,"\nsubgraph cluster_%1$d { rank=same\n",ltv);
    RBR_traverse(&ltv->rbr,cluster_rbn,NULL);
    RBR_traverse(&ltv->rbr,cluster_rbn2,NULL);
    fprintf(dumpfile,"}\n");
    fprintf(dumpfile,"%1$d -> %2$d\n\n",ltv,ltv->rbr.rb_node);
    return NULL;
}

void *LTOBJ_dump_pre(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
{
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltobj_data) return NULL;
    
    if (ltv)
    {
        if (ltv->len)
        {
            fprintf(dumpfile,"%d [style=filled shape=box label=\"",ltv);
            fstrnprint(dumpfile,ltv->data,ltv->len);
            fprintf(dumpfile,"\"]\n");
        }
        else
            fprintf(dumpfile,"%d [label=\"\" shape=box style=filled height=.1 width=.1]\n",ltv);
        
        if (ltv->rbr.rb_node)
            cluster_ltv(ltv,NULL);
    }

    if (lti)
    {
        CLL *lnk;
#if 0 // handled in cluster_rbn now
        fprintf(dumpfile,"%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn))
            fprintf(dumpfile,"%d -> %d\n",rb_parent(&lti->rbn),&lti->rbn);
#endif
        if (lnk=CLL_get(&lti->cll,0,0))
            fprintf(dumpfile,"%d -> %d\n",&lti->rbn,lnk);
    }

    if (ltvr)
    {
        fprintf(dumpfile,"%d [label=\"\" shape=point]\n",ltvr);
        if (ltvr->ltv) fprintf(dumpfile,"%d -> %d\n",ltvr,ltvr->ltv);
        if (ltvr->cll.lnk[1]!=ltobj_data->data)
            fprintf(dumpfile,"%d -> %d\n",ltvr->cll.lnk[1],ltvr);
    }
    
    return NULL;
}


extern int Gmymalloc;

int edict_dump(EDICT *edict)
{
    int status;
    struct LTOBJ_DATA ltobj_data = { NULL, NULL };;
    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\n\tcompund=true ordering=out concentrate=true\n\tnode [shape=record]\n\tedge []\n");
    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"%d [label=\"\" shape=point]\n",&edict->dict);

    ltobj_data.preop = LTOBJ_dump_pre;
    ltobj_data.postop = NULL;
    TRY(CLL_traverse(&edict->dict,0,LTVR_traverse,&ltobj_data),0,finish,"\n");
    
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
