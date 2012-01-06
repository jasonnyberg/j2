/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


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


void *edict_traverse(CLL *cll,LTOBJ_OP preop,LTOBJ_OP postop)
{
    void *rval=NULL;
    struct LTOBJ_DATA ltobj_data = { preop,postop,0,NULL,0 };
    void *ltvr_op(CLL *cll,void *data) { return LTVR_traverse(cll,data); }
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
    fprintf(dumpfile,"%1$d [label=\"toks\" color=blue] %1$d -> %2$d\n",&edict->toks,edict->toks.lnk[0]);

    edict_traverse(&edict->dict,LTOBJ_graph_pre,NULL);
    edict_traverse(&edict->anon,LTOBJ_graph_pre,NULL);
    edict_traverse(&edict->toks,LTOBJ_graph_pre,NULL); 
    
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
    
    struct LTOBJ_DATA ltobj_data = { LTOBJ_print_pre,NULL,0,NULL,0 };
    edict_traverse(&edict->anon,LTOBJ_print_pre,NULL);
    edict_traverse(&edict->dict,LTOBJ_print_pre,NULL);
    return status;
}


int main()
{
    EDICT edict;
    edict_init(&edict,LTV_new("ROOT",-1,0));
    edict_repl(&edict);
    edict_destroy(&edict);
}
