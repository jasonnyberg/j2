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
        fprintf(dumpfile,"%1$d -> %2$d [color=blue lhead=cluster_%2$d]\n\n",ltv,ltv->rbr.rb_node);
    }
    
    return NULL;
}

void *LTOBJ_graph_pre(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
{
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltobj_data) goto done;
    
    if (ltv)
    {
        if (ltv->flags&LT_AVIS && (ltobj_data->halt=1)) goto done;
        
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
        if (ltvr->ltv) fprintf(dumpfile,"%d -> %d [weight=2]\n",ltvr,ltvr->ltv);
        fprintf(dumpfile,"%d [label=\"\" shape=point color=brown]\n",&ltvr->cll);
        fprintf(dumpfile,"%d -> %d [color=brown]\n",&ltvr->cll,ltvr->cll.lnk[0]);
    }
    
    if (lti)
    {
        fprintf(dumpfile,"%d [label=\"\" shape=point color=red]\n",&lti->cll);
        fprintf(dumpfile,"%d -> %d [weight=2]\n",&lti->rbn,&lti->cll);
        fprintf(dumpfile,"%d -> %d [color=red]\n",&lti->cll,lti->cll.lnk[0]);
    }

 done:
    return NULL;
}

char *indent="                                                                                                                ";
extern int Gmymalloc;


void *edict_traverse(CLL *cll,LTOBJ_OP preop,LTOBJ_OP postop)
{
    void *rval=NULL;
    struct LTOBJ_DATA ltobj_data = {preop, postop, 0, 0, NULL };
    rval=CLL_traverse(cll,0,LTVR_traverse,&ltobj_data);
    CLL_traverse(cll,0,LTVR_traverse,NULL); // cleanup "visited" flags
    return rval;
}


int edict_dump(EDICT *edict)
{
    int status=0;
    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\n\tnode [shape=record]\n\tedge []\n");

    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"ltv_count [label=\"ltv_count %d\"]\n",ltv_count);
    fprintf(dumpfile,"ltvr_count [label=\"ltvr_count %d\"]\n",ltvr_count);
    fprintf(dumpfile,"lti_count [label=\"lti_count %d\"]\n",lti_count);
    fprintf(dumpfile,"%1$d [label=\"dict\" color=blue] %1$d -> %2$d\n",&edict->dict,edict->dict.lnk[0]);
    fprintf(dumpfile,"%1$d [label=\"anon\" color=blue] %1$d -> %2$d\n",&edict->anon,edict->anon.lnk[0]);
    fprintf(dumpfile,"%1$d [label=\"code\" color=blue] %1$d -> %2$d\n",&edict->code,edict->code.lnk[0]);

    edict_traverse(&edict->dict,LTOBJ_graph_pre,NULL);
    edict_traverse(&edict->anon,LTOBJ_graph_pre,NULL);
    edict_traverse(&edict->code,LTOBJ_graph_pre,NULL); 
    
    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

    return status;
}

int edict_print(EDICT *edict,char *name,int len)
{
    int ltv_prints=0;
    LTI *_lti=NULL;

    void *LTOBJ_print_pre(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
    {
        struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
        if (!ltobj_data) goto done;
    
        if (ltv)
        {
            if (_lti) ltobj_data->halt=1;
            fstrnprint(stdout,indent,ltobj_data->depth*4+2);
            fprintf(stdout,"[");
            fstrnprint(stdout,ltv->data,ltv->len);
            fprintf(stdout,"]\n");
            ltv_prints++;
        }
    
        if (lti)
        {
            if (!lti) ltobj_data->halt=1;
            fstrnprint(stdout,indent,ltobj_data->depth*4);
            fprintf(stdout,"\"%s\"\n",lti->name);
        }

 done:
        return NULL;
    }

    int status=0;
    void *md;
    LTV *ltv=(name && len)?edict_get(edict,name,len,0,&md,&_lti):NULL;
    struct LTOBJ_DATA ltobj_data = { LTOBJ_print_pre, NULL, 0, 0, NULL };
    if (_lti)
        LTI_traverse((RBN *) _lti,&ltobj_data),LTI_traverse((RBN *) _lti,NULL);
    else
        edict_traverse(&edict->anon,LTOBJ_print_pre,NULL);
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
