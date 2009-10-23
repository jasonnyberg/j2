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
            fprintf(dumpfile,"\t%d -> %d [color=blue]\n",rb_parent(&lti->rbn),&lti->rbn);
        return NULL;
    }
    if (ltv->rbr.rb_node)
    {
        //fprintf(dumpfile,"\nsubgraph cluster_%d { ratio=4\n",&ltv->rbr.rb_node);
        RBR_traverse(&ltv->rbr,cluster_rbn,NULL);
        RBR_traverse(&ltv->rbr,cluster_rbn2,NULL);
        //fprintf(dumpfile,"}\n");
        fprintf(dumpfile,"%1$d -> %2$d [lhead=cluster_%2$d]\n\n",ltv,ltv->rbr.rb_node);
    }
    return NULL;
}

void *LTOBJ_graph_pre(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
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
            fprintf(dumpfile,"%d [label=\"\" shape=box style=filled height=.1 width=.3]\n",ltv);
        
        cluster_ltv(ltv,NULL);
    }

    if (ltvr)
    {
        if (ltvr->ltv) fprintf(dumpfile,"%d -> %d\n",ltvr,ltvr->ltv);
        fprintf(dumpfile,"%d [label=\"\" shape=point color=brown]\n",&ltvr->cll);
        fprintf(dumpfile,"%d -> %d [color=brown]\n",&ltvr->cll,ltvr->cll.lnk[0]);
    }
    
    if (lti)
    {
        fprintf(dumpfile,"%d [label=\"\" shape=point color=red]\n",&lti->cll);
        fprintf(dumpfile,"%d -> %d\n",&lti->rbn,&lti->cll);
        fprintf(dumpfile,"%d -> %d [color=red]\n",&lti->cll,lti->cll.lnk[0]);
    }

    return NULL;
}

char *indent="                                                                                                                ";


void *LTOBJ_print_pre(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
{
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltobj_data) return NULL;
    
    if (ltv)
    {
        fstrnprint(stdout,indent,ltobj_data->depth*4+1);
        fstrnprint(stdout,ltv->data,ltv->len);
        fprintf(stdout,"\n");
    }
    
    if (lti)
    {
        fstrnprint(stdout,indent,ltobj_data->depth*4);
        fprintf(stdout,"%s:\n",lti->name);
    }

    return NULL;
}

extern int Gmymalloc;

int edict_dump(EDICT *edict)
{
    int status;
    struct LTOBJ_DATA ltobj_data = { NULL, NULL, 0, NULL };
    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\n\tnode [shape=record]\n\tedge []\n");
    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"ltv_count [label=\"ltv_count %d\"]\n",ltv_count);
    fprintf(dumpfile,"ltvr_count [label=\"ltvr_count %d\"]\n",ltvr_count);
    fprintf(dumpfile,"lti_count [label=\"lti_count %d\"]\n",lti_count);
    fprintf(dumpfile,"%1$d [label=\"\" shape=point color=blue] %1$d -> %2$d\n",&edict->dict,edict->dict.lnk[0]);

    ltobj_data.preop = LTOBJ_graph_pre;
    ltobj_data.postop = NULL;
    TRY(CLL_traverse(&edict->dict,0,LTVR_traverse,&ltobj_data),0,finish,"\n");
    TRY(CLL_traverse(&edict->dict,0,LTVR_traverse,NULL),0,finish,"\n");
    
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
