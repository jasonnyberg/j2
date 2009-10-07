#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

FILE *dumpfile;

void *LTOBJ_dump(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
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
            fprintf(dumpfile,"%d -> %d\n",ltv,ltv->rbr.rb_node);
    }

    if (lti)
    {
        CLL *lnk;
        fprintf(dumpfile,"%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn))
            fprintf(dumpfile,"%d -> %d\n",rb_parent(&lti->rbn),&lti->rbn);
        if (lnk=CLL_get(&lti->cll,0,0))
            fprintf(dumpfile,"%d -> %d\n",&lti->rbn,lnk);
    }

    if (ltvr)
    {
        struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
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
    struct LTOBJ_DATA ltobj_data = { LTOBJ_dump, NULL };
    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\n\tordering=out concentrate=true\n\tnode [shape=record]\n\tedge []\n");
    //fprintf(dumpfile,"root=\"%d\"\n",-1);
    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"%d [label=dict]\n",&edict->dict);
    
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
