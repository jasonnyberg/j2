#include <stdio.h>
#include <string.h>
#include "util.h"
#include "edict.h"

FILE *dumpfile;


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
        
        if (ltv->rbr.rb_node)
            fprintf(dumpfile,"%1$d -> %2$d [color=blue lhead=cluster_%2$d]\n\n",ltv,ltv->rbr.rb_node);
    }

    if (ltvr)
    {
        if (ltvr->ltv) fprintf(dumpfile,"%d -> %d [weight=2]\n",ltvr,ltvr->ltv);
        fprintf(dumpfile,"%d [label=\"\" shape=point color=brown]\n",&ltvr->cll);
        fprintf(dumpfile,"%d -> %d [color=brown]\n",&ltvr->cll,ltvr->cll.lnk[0]);
    }
    
    if (lti)
    {
        fprintf(dumpfile,"\t%d [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn)) fprintf(dumpfile,"\t%d -> %d [color=blue]\n",rb_parent(&lti->rbn),&lti->rbn);
        fprintf(dumpfile,"%d [label=\"\" shape=point color=red]\n",&lti->cll);
        fprintf(dumpfile,"%d -> %d [weight=2]\n",&lti->rbn,&lti->cll);
        fprintf(dumpfile,"%d -> %d [color=red]\n",&lti->cll,lti->cll.lnk[0]);
    }

 done:
    return NULL;
}

char *indent="                                                                                                                ";
extern int Gmymalloc;


void *edict_traverse(CLL *cll,char *pat,int len,LTOBJ_OP preop,LTOBJ_OP postop)
{
    void *rval=NULL;
    struct LTOBJ_DATA ltobj_data = { preop,postop,0,NULL,0,0,0 };
    void *ltvr_op(CLL *cll,void *data) { return LTVR_traverse(cll,pat,len,data); }
    rval=CLL_traverse(cll,0,ltvr_op,&ltobj_data);
    CLL_traverse(cll,0,ltvr_op,NULL); // cleanup "visited" flags
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

    edict_traverse(&edict->dict,NULL,0,LTOBJ_graph_pre,NULL);
    edict_traverse(&edict->anon,NULL,0,LTOBJ_graph_pre,NULL);
    edict_traverse(&edict->code,NULL,0,LTOBJ_graph_pre,NULL); 
    
    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

    return status;
}

int edict_print(EDICT *edict,char *name,int len)
{
    LTI *_lti=NULL;
    unsigned uval=0;

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
        }
        if (lti)
        {
            if (!_lti && ltobj_data->depth>uval) ltobj_data->halt=1;
            fstrnprint(stdout,indent,ltobj_data->depth*4);
            fprintf(stdout,"\"%s\"\n",lti->name);
        }
 done:
        return NULL;
    }

    int status=0;
    void *md;
    LTV *ltv=NULL;

    if (name && len && !strtou(name,len,&uval))
        ltv=edict_get(edict,name,len,0,&md,&_lti);
    
    struct LTOBJ_DATA ltobj_data = { LTOBJ_print_pre,NULL,0,NULL,0,0 };
    if (_lti)
        LTI_traverse((RBN *) _lti,name,len,&ltobj_data),
            LTI_traverse((RBN *) _lti,name,len,NULL);
    else
        edict_traverse(&edict->anon,name,len,LTOBJ_print_pre,NULL);
    return status;
}


int edict_match(EDICT *edict,char *name,int len)
{
    int offset=0,toffset=0;
    CLL ltis;
    
    void *dump_lti(CLL *cll,void *data)
    {
        LTVR *ltvr=(LTVR *) cll;
        LTI *lti=NULL;
        if (ltvr && ltvr->ltv && (lti=ltvr->ltv->data))
            printf("%s",lti->name);
        return NULL;
    }
    
    void *LTOBJ_match_pre(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
    {
        struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
        if (!ltobj_data) goto done;

        int nlen=strncspn(name+toffset,len-toffset,"."); // end of layer name
        int tlen=strncspn(name+toffset,len-toffset,"=+"); // specific value

        if (lti)
        {
            toffset=offset;
            LTV_put(&ltis,LTV_new(lti,0,0),0,(void *) offset);
            offset+=nlen+1;
        }

        if (ltobj_data->halt=toffset>=len)
            goto done;
        
        if (lti)
            ltobj_data->halt=fnmatch_len(name+toffset,len-toffset,lti->name,MIN(tlen,nlen))!=0;
        if (ltv)
        {
            if (offset>=len)
            {
                CLL_traverse(&ltis,1,dump_lti,NULL);
                printf(":%s\n",ltv->data);
            }
        }
     done:
        return NULL;
    }
    
    void *LTOBJ_match_post(LTVR *ltvr,LTI *lti,LTV *ltv,void *data)
    {
        void *md=NULL;
        if (!data) goto done;
        if (lti)
        {
            LTV_release(LTV_get(&ltis,1,0,NULL,-1,&md));
            toffset=offset=(int) md;
        }
     done:
        return NULL;
    }

    int status=0;
    CLL_init(&ltis);
    if (name && len)
        edict_traverse(&edict->dict,name,len,LTOBJ_match_pre,LTOBJ_match_post);
    
    return status;
}


EDICT edict;

int main()
{
    edict_init(&edict,LTV_new("ROOT",-1,0));
    edict_repl(&edict);
    edict_destroy(&edict);
}
