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


#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include "util.h"
#include "listree.h"

#include "trace.h" // lttng

CLL ltv_repo,ltvr_repo,lti_repo,ro_list;
int ltv_count=0,ltvr_count=0,lti_count=0;

//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

RBR *RBR_init(RBR *rbr)
{
    RB_EMPTY_ROOT(rbr);
    return rbr;
}

void RBN_release(RBR *rbr,RBN *rbn,void (*rbn_release)(RBN *rbn))
{
    TSTART(0,"");
    rb_erase(rbn,rbr);
    if (rbn_release)
        rbn_release(rbn);
    TFINISH(0,"");
}

void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn))
{
    TSTART(0,"");
    RBN *rbn;
    while (rbn=rbr->rb_node)
        RBN_release(rbr,rbn,rbn_release);
    TFINISH(0,"");
}

// return node that owns "name", inserting if desired AND required
LTI *RBR_find(RBR *rbr,char *name,int len,int *insert)
{
    LTI *lti=NULL;
    if (rbr && name) {
        if (series(name,len,NULL,"*?",NULL)<len) { // contains wildcard, do not insert
            for (lti=(LTI *) rb_first(rbr); lti && fnmatch_len(name,len,lti->name,-1); lti=LTI_next(lti));
        } else {
            RBN *parent=NULL,**rbn = &rbr->rb_node;
            while (*rbn) {
                int result = strnncmp(name,len,((LTI *) *rbn)->name,-1);
                if (!result) return (LTI *) *rbn; // found it!
                else (parent=*rbn),(rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right);
            }
            if (insert) {
                if (*insert && (lti=LTI_new(name,len))) {
                    rb_link_node(&lti->rbn,parent,rbn); // add
                    rb_insert_color(&lti->rbn,rbr); // rebalance
                } else {
                    *insert=0;
                }
            }
        }
    }
    return lti;
}


// get a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,LTV_FLAGS flags)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&ltv_repo);

    LTV *ltv=NULL;
    if ((ltv=(LTV *) CLL_get(repo,POP,TAIL)) || (ltv=NEW(LTV))) {
        ZERO(*ltv);
        ltv_count++;
        ltv->len=(len<0 && !(flags&LT_NSTR))?strlen((char *) data):len;
        ltv->data=data;
        if (flags&LT_DUP) ltv->data=bufdup(ltv->data,ltv->len);
        if (flags&LT_ESC) strstrip(ltv->data,&ltv->len);
        if (flags&LT_LIST) CLL_init(&ltv->sub.ltvs);
        ltv->flags=flags;
    }
    return ltv;
}

void LTV_free(LTV *ltv)
{
    if (ltv) {
        if (ltv->flags&LT_FREE && !(ltv->flags&LT_IMM)) DELETE(ltv->data);
        ZERO(*ltv);
        CLL_put(&ltv_repo,ltv->repo,HEAD);
        ltv_count--;
    }
}

void *LTV_map(LTV *ltv,int reverse,RB_OP rb_op,CLL_OP cll_op)
{
    RBN *rbn=NULL,*next;
    void *result=NULL;
    if (ltv) {
        if (ltv->flags&LT_LIST && cll_op) result=CLL_map(&ltv->sub.ltvs,FWD,cll_op);
        else if (rb_op) {
            RBR *rbr=&ltv->sub.ltis;
            if (reverse) for (rbn=rb_last(rbr); rbn && (next=rb_prev(rbn),!(result=rb_op(rbn)));rbn=next);
            else         for (rbn=rb_first(rbr);rbn && (next=rb_next(rbn),!(result=rb_op(rbn)));rbn=next);
        }
    }
    return result;
}


// get a new LTVR
LTVR *LTVR_new(LTV *ltv)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&ltvr_repo);

    LTVR *ltvr=(LTVR *) CLL_get(repo,POP,TAIL);
    if (ltvr || (ltvr=NEW(LTVR))) {
        ZERO(*ltvr);
        ltvr->ltv=ltv;
        ltv->refs++;
        ltvr_count++;
    }
    return ltvr;
}

LTV *LTVR_free(LTVR *ltvr)
{
    LTV *ltv=NULL;
    if (ltvr) {
        if (!CLL_EMPTY(&ltvr->lnk)) { CLL_cut(&ltvr->lnk); }
        if ((ltv=ltvr->ltv))
            ltv->refs--;
        CLL_put(&ltvr_repo,ltvr->repo,HEAD);
        ltvr->flags=0;
        ltvr_count--;
    }
    return ltv;
}


// get a new LTI and prepare for insertion
LTI *LTI_new(char *name,int len)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&lti_repo);

    LTI *lti;
    if (name && ((lti=(LTI *) CLL_get(repo,POP,TAIL)) || (lti=NEW(LTI)))) {
        ZERO(*lti);
        lti_count++;
        lti->name=bufdup(name,len);
        CLL_init(&lti->ltvs);
    }
    return lti;
}

void LTI_free(LTI *lti)
{
    if (lti) {
        DELETE(lti->name);
        ZERO(*lti);
        CLL_put(&lti_repo,lti->repo,HEAD);
        lti_count--;
    }
}


//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && !ltv->refs) {
        if (ltv->flags&LT_LIST) CLL_release(&ltv->sub.ltvs,LTVR_release);
        else                    RBR_release(&ltv->sub.ltis,LTI_release);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *lnk) { LTV_release(LTVR_free((LTVR *) lnk)); }

void LTI_release(RBN *rbn) {
    LTI *lti=(LTI *) rbn;
    if (lti) {
        CLL_release(&lti->ltvs,LTVR_release);
        LTI_free(lti);
    }
}


//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

void *listree_traverse(LTV *ltv,LTOBJ_OP preop,LTOBJ_OP postop)
{
    int depth=0,flags=0,cleanup=0;
    void *rval=NULL;

    void *LTV_traverse(LTV *ltv) {
        void *LTVR_traverse(CLL *lnk) {
            LTI *lti=NULL; LTVR *ltvr=(LTVR *) lnk; LTV *ltv=NULL;
            if (!ltvr) goto done;

            if (cleanup && ltvr->ltv) LTV_traverse(ltvr->ltv);
            else if (preop && (rval=preop(&lti,&ltvr,&ltv,depth,&flags)) ||
                     ((flags&LT_TRAVERSE_HALT) || (rval=LTV_traverse(ltv?ltv:ltvr->ltv))) ||
                     postop && (rval=postop(&lti,&ltvr,&ltv,depth,&flags)))
                goto done;

            done:
            flags=0;
            return rval;
        }

        void *LTI_traverse(RBN *rbn) {
            LTI *lti=(LTI *) rbn; LTVR *ltvr=NULL; LTV *ltv=NULL;
            if (!lti) goto done;

            if (cleanup) CLL_map(&lti->ltvs,FWD,LTVR_traverse);
            else if (preop && (rval=preop(&lti,&ltvr,&ltv,depth,&flags)) ||
                     ((flags&LT_TRAVERSE_HALT) || (rval=ltvr?LTVR_traverse(&ltvr->lnk):CLL_map(&lti->ltvs,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,LTVR_traverse))) ||
                     postop && (rval=postop(&lti,&ltvr,&ltv,depth,&flags)))
                goto done;

            done:
            flags=0;
            return rval;
        }

        LTI *lti=NULL; LTVR *ltvr=NULL; // LTV *ltv defined in args
        if (!ltv) goto done;

        if (cleanup) // remove absolute visited flag
            return (ltv->flags&LT_AVIS && !((ltv->flags&=~LT_AVIS)&LT_AVIS))? LTV_map(ltv,FWD,LTI_traverse,LTVR_traverse):NULL;
        else if (!(ltv->flags&LT_RVIS)) {
            if (preop && (rval=preop(&lti,&ltvr,&ltv,depth,&flags))) goto done;

            if (flags&LT_TRAVERSE_HALT) goto done;
            ltv->flags|=LT_RVIS;
            depth++;
            rval=lti?LTI_traverse(&lti->rbn):LTV_map(ltv,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,LTI_traverse,LTVR_traverse);
            depth--;
            ltv->flags&=~LT_RVIS;
            if (rval) goto done;

            if (postop && (rval=postop(&lti,&ltvr,&ltv,depth,&flags))) goto done;
        }

        done:
        if (ltv) ltv->flags|=LT_AVIS;
        flags=0;
        return rval;
    }

    rval=LTV_traverse(ltv);
    cleanup=1;
    preop=postop=NULL;
    LTV_traverse(ltv); // clean up "visited" flags
    return rval;
}

//////////////////////////////////////////////////
// Basic LT insert/remove
//////////////////////////////////////////////////

extern LTI *LTV_first(LTV *ltv) { return (!ltv || (ltv->flags&LT_LIST))?NULL:(LTI *) rb_first((RBR *) &ltv->sub.ltis); }
extern LTI *LTV_last(LTV *ltv)  { return (!ltv || (ltv->flags&LT_LIST))?NULL:(LTI *) rb_last((RBR *)  &ltv->sub.ltis); }

extern LTI *LTI_next(LTI *lti) { return lti? (LTI *) rb_next((RBN *) lti):NULL; }
extern LTI *LTI_prev(LTI *lti) { return lti? (LTI *) rb_prev((RBN *) lti):NULL; }

LTV *LTV_put(CLL *ltvs,LTV *ltv,int end,LTVR **ltvr_ret)
{
    int status=0;
    LTVR *ltvr=NULL;
    if (ltvs && ltv && (ltvr=LTVR_new(ltv))) {
        if (CLL_put(ltvs,&ltvr->lnk,end)) {
            if (ltvr_ret) *ltvr_ret=ltvr;
            return ltv; //!!
        }
        else LTVR_free(ltvr);
    }
    return NULL;
}

LTV *LTV_get(CLL *ltvs,int pop,int dir,LTV *match,LTVR **ltvr_ret)
{
    void *ltv_match(CLL *lnk) {
        LTVR *ltvr=(LTVR *) lnk;
        if (!ltvr || !ltvr->ltv || ltvr->ltv->flags&LT_IMM || fnmatch_len(ltvr->ltv->data,ltvr->ltv->len,match->data,match->len)) return NULL;
        else return pop?CLL_cut(lnk):lnk;
    }

    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (!(ltvr=(LTVR *) match?CLL_mapfrom(ltvs,ltvr_ret?*ltvr_ret:NULL,dir,ltv_match):CLL_get(ltvs,pop,dir)))
        return NULL;
    ltv=ltvr->ltv;
    if (pop) {
        LTVR_free(ltvr);
        ltvr=NULL;
    }
    if (ltvr_ret) (*ltvr_ret)=ltvr;
    return ltv;
}

LTV *LTV_dup(LTV *ltv)
{
    if (!ltv) return NULL;

    int flags=ltv->flags & ~LT_FREE;
    if (!(flags&LT_IMM))
        flags |= LT_DUP;
    return LTV_new(ltv->data,ltv->len,flags);
}

LTV *LTV_enq(CLL *ltvs,LTV *ltv,int end) { return LTV_put(ltvs,ltv,end,NULL); }
LTV *LTV_deq(CLL *ltvs,int end)          { return LTV_get(ltvs,POP,end,NULL,NULL); }
LTV *LTV_peek(CLL *ltvs,int end)         { return LTV_get(ltvs,KEEP,end,NULL,NULL); }

int LTV_wildcard(LTV *ltv)
{
    int tlen=series(ltv->data,ltv->len,NULL,"*?",NULL);
    return tlen < ltv->len;
}

void print_ltv(FILE *ofile,char *pre,LTV *ltv,char *post,int maxdepth)
{
    char *indent="                                                                                                                ";
    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        if (*lti) {
            if (maxdepth && depth>=maxdepth) *flags|=LT_TRAVERSE_HALT;
            fstrnprint(stdout,indent,MAX(0,depth*4-2));
            fprintf(ofile,"\"%s\"\n",(*lti)->name);
        }

        if (*ltv) {
            fstrnprint(ofile,indent,depth*4);
            if (pre) fprintf(ofile,"%s",pre);
            else fprintf(ofile,"[");
            if (((*ltv)->flags&LT_IMM)==LT_IMM) fprintf(ofile,"0x%p (immediate)",&(*ltv)->data);
            else if ((*ltv)->flags&LT_NULL)     ; // nothing
            else if ((*ltv)->flags&LT_NIL)      fprintf(ofile,"nil");
            else if ((*ltv)->flags&LT_BIN)      hexdump(ofile,(*ltv)->data,(*ltv)->len);
            else                                fstrnprint(ofile,(*ltv)->data,(*ltv)->len);
            if (post) fprintf(ofile,"%s",post);
            else fprintf(ofile,"]\n");
        }
        return NULL;
    }

    if (ltv)
        listree_traverse(ltv,preop,NULL);
    else
        fprintf(ofile,"NULL");
}

void print_ltvs(FILE *ofile,char *pre,CLL *ltvs,char *post,int maxdepth)
{
    void *op(CLL *lnk) { LTVR *ltvr=(LTVR *) lnk; if (ltvr) print_ltv(ofile,pre,(LTV *) ltvr->ltv,post,maxdepth); return NULL; }
    if (pre) fprintf(ofile,"%s",pre);
    CLL_map(ltvs,FWD,op);
    if (post) fprintf(ofile,"%s",post);
}


void ltvs2dot(FILE *ofile,CLL *ltvs,int maxdepth,char *label) {
    int i=0;
    int halt=0;

    void ltvs2dot(CLL *ltvs,char *label) {
        if (label)
            fprintf(ofile,"\"%1$s_%2$x\" [label=\"%1$s\" shape=ellipse color=blue]\n\"%1$s_%2$x\" -> \"%2$x\"\n",label,ltvs);
        fprintf(ofile,"\"%x\" [label=\"\" shape=point color=red]\n",ltvs);
        fprintf(ofile,"\"%x\" -> \"%x\" [color=red]\n",ltvs,ltvs->lnk[0]);
    }

    void lti2dot(LTI *lti,int depth,int *flags) {
        fprintf(ofile,"\"%x\" [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn)) fprintf(ofile,"\"%x\" -> \"%x\" [color=blue weight=0]\n",rb_parent(&lti->rbn),&lti->rbn);
        fprintf(ofile,"\"%x\" -> \"%x\" [weight=2]\n",&lti->rbn,&lti->ltvs);
        ltvs2dot(&lti->ltvs,NULL);
    }

    void ltvr2dot(LTVR *ltvr,int depth,int *flags) {
        if (ltvr->ltv) fprintf(ofile,"\"%x\" -> \"%x\" [weight=2]\n",ltvr,ltvr->ltv);
        fprintf(ofile,"\"%x\" [label=\"\" shape=point color=brown]\n",&ltvr->lnk);
        fprintf(ofile,"\"%x\" -> \"%x\" [color=brown]\n",&ltvr->lnk,ltvr->lnk.lnk[0]);
    }

    void ltv2dot(LTV *ltv,int depth,int *flags) {
        if (ltv->len && !(ltv->flags&LT_NSTR)) {
            fprintf(ofile,"\"%x\" [style=filled shape=box label=\"",ltv);
            fstrnprint(ofile,ltv->data,ltv->len);
            fprintf(ofile,"\"]\n");
        }
        else if ((ltv->flags&LT_IMM)==LT_IMM)
            fprintf(ofile,"\"%x\" [label=\"I(%x)\" shape=box style=filled]\n",ltv,ltv->data);
        else if (ltv->flags&LT_NULL)
            fprintf(ofile,"\"%x\" [label=\"\" shape=box style=filled]\n",ltv);
        else if (ltv->flags&LT_NIL)
            fprintf(ofile,"\"%x\" [label=\"NIL\" shape=box style=filled]\n",ltv);
        else
            fprintf(ofile,"\"%x\" [label=\"\" shape=box style=filled height=.1 width=.3]\n",ltv);

        fprintf(ofile,"subgraph cluster_%d { subgraph { rank=same\n",i++);
        for (LTI *lti=LTV_first(ltv);lti;lti=LTI_next(lti))
            fprintf(ofile,"\"%x\"\n",lti);
        fprintf(ofile,"}}\n");

        if (ltv->sub.ltis.rb_node)
            fprintf(ofile,"\"%1$x\" -> \"%2$x\" [color=blue weight=0]\n",ltv,ltv->sub.ltis.rb_node);
    }

    void descend_ltv(LTV *ltv) {
        void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
            if (*lti) {
                if (maxdepth && depth>=maxdepth)
                    *flags|=LT_TRAVERSE_HALT;
                else
                    lti2dot(*lti,depth,flags);
            }
            else if (*ltvr) ltvr2dot(*ltvr,depth,flags);
            else if (*ltv)  ltv2dot(*ltv,depth,flags);
            return NULL;
        }

        listree_traverse(ltv,preop,NULL);
    }

    void descend_ltvr(LTVR *ltvr) {
        ltvr2dot(ltvr,0,&halt);
        descend_ltv(ltvr->ltv);
    }

    void *op(CLL *lnk) { descend_ltvr((LTVR *) lnk); return NULL; }
    ltvs2dot(ltvs,label);
    CLL_map(ltvs,FWD,op);
}

void graph_ltvs(FILE *ofile,CLL *ltvs,int maxdepth,char *label) {
    fprintf(ofile,"digraph iftree\n{\ngraph [/*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");
    ltvs2dot(ofile,ltvs,maxdepth,label);
    fprintf(ofile,"}\n");
}

void graph_ltvs_to_file(char *filename,CLL *ltvs,int maxdepth,char *label) {
    FILE *ofile=fopen(filename,"w");
    graph_ltvs(ofile,ltvs,maxdepth,label);
    fclose(ofile);
}



//////////////////////////////////////////////////
// REF
// special case; client creates ref, which acts as sentinel
//////////////////////////////////////////////////


CLL ref_repo;
int ref_count=0;

REF *refpush(CLL *cll,REF *ref) { return (REF *) CLL_put(cll,&ref->lnk,HEAD); }
REF *refpop(CLL *cll)           { return (REF *) CLL_get(cll,POP,HEAD); }

REF *REF_new(char *data,int len)
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&ref_repo);
    int rev=data[0]=='-';
    if (len-rev==0)
        return NULL;

    REF *ref=NULL;
    if ((ref=refpop(repo)) || ((ref=NEW(REF)) && CLL_init(&ref->lnk)))
    {
        CLL_init(&ref->keys);
        LTV_enq(&ref->keys,LTV_new(data+rev,len-rev,0),TAIL);
        CLL_init(&ref->root);
        ref->lti=NULL;
        ref->ltvr=NULL;
        ref->reverse=rev;
        ref_count++;
    }
    return ref;
}

void *REF_reset(CLL *lnk) {
    REF *ref=(REF *) lnk;
    LTV *root=LTV_peek(&ref->root,HEAD);
    if (root && ref->lti && CLL_EMPTY(&ref->lti->ltvs)) // if LTI empty and pruneable
        RBN_release(&root->sub.ltis,&ref->lti->rbn,LTI_release); // prune it
    ref->lti=NULL;
    ref->ltvr=NULL;
    CLL_release(&ref->root,LTVR_release);
}

void REF_free(CLL *lnk)
{
    if (!lnk) return;

    REF *ref=(REF *) lnk;
    CLL_cut(&ref->lnk); // take it out of any list it's in
    CLL_release(&ref->keys,LTVR_release);
    REF_reset(lnk);

    refpush(&ref_repo,ref);
    ref_count--;
}

///////////////////////////////////////////////////////

int REF_create(LTV *ltv,CLL *refs) {
    int status=0;
    STRY(!ltv || !refs,"validating params");
    STRY(REF_delete(refs),"clearing any refs");

    char *data=ltv->data;
    int len=ltv->len;

    int advance(int bump) { bump=MIN(bump,len); data+=bump; len-=bump; return bump; }

    int name() { return series(data,len,NULL,".[",NULL); }
    int val()  { return series(data,len,NULL,NULL,"[]"); } // val=data+1,len-2!!!
    int sep()  { return series(data,len,".",NULL,NULL);  }

    unsigned tlen;
    REF *ref=NULL;

    while (len) { // parse ref keys
        // parse name (mandatory)
        STRY(!(tlen=name()),"parsing ref name");
        STRY(!(ref=REF_new(data,tlen)),"allocating name ref");
        STRY(!CLL_put(refs,&ref->lnk,HEAD),"enqueing name ref");
        advance(tlen);

        // parse vals (optional)
        while ((tlen=val()))
            STRY(!LTV_enq(&ref->keys,LTV_new(data+1,tlen-2,0),TAIL),"enqueueing val key");

        // if there's anything left, it has to be a separator
        if (len)
            STRY(advance(sep())==1,"parsing sep");
    }

    done:
    return status;
}


int REF_delete(CLL *refs) {
    void release(CLL *lnk) { REF_free(lnk); }
    CLL_release(refs,release);
}

void REF_dump(FILE *ofile,CLL *refs)
{
    void *dump(CLL *lnk)
    {
        REF *ref=(REF *) lnk;
        print_ltvs(ofile,"root(",&ref->root,")\n",1);
        print_ltvs(ofile,"keys(",&ref->keys,") ",1);
        fprintf(ofile,"lti(%x) ",ref->lti);
        print_ltv(ofile,"ltv(",ref->ltvr?ref->ltvr->ltv:NULL,")\n",1);
    }

    CLL_map(refs,REV,dump); // then the rest
}

LTI *LTV_lookup(LTV *root,LTV *name,int *insert)
{
    int status=0;
    LTI *lti=NULL;
    STRY(!root || !name,"validating arguments");

    if (LTV_wildcard(name)) {
        for (lti=LTI_first(ref->lti); lti && fnmatch_len(name->data,name->len,lti->name,-1); lti=LTI_next(lti)) {}
        ref->lti=lti;
    } else {
        STRY(!(lti=(*lti)=RBR_find(&root->sub.ltis,name->data,name->len,insert)),"looking up lti");
    }
    done:
    return status?NULL:lti;
}

int REF_resolve(CLL *refs,LTV *root,int insert)
{
    int status=0;
    LTV *cur_root=root;

    int resolve_key(REF *ref,LTV *root) {
        int status=0;
        LTV *name=NULL;
        LTVR *ltvr=NULL;
        STRY(!ref || !root,"validating args");
        STRY(!(name=LTV_get(&ref->keys,KEEP,HEAD,NULL,&ltvr)),"validating name key"); // name is first key (use get for ltvr)
        val=(LTV *) CLL_next(&ref->keys,&ltvr->lnk,FWD); // val will be next key

        if (root->flags&LT_CVAR) {
            // process CVAR
        } else {
            if (!ref->lti) {
                int inserted=insert;
                STRY(!(ref->lti=LTV_lookup(root,name,&inserted)),"looking up lti");
            }
            if (ref->lti && !ref->ltvr) {
                char *match=val?val->data:NULL;
                int matchlen=val?val->len:0;
                STRY(!(LTV_get(&ref->lti->ltvs,KEEP,ref->reverse,match,matchlen,&ref->ltvr),"retrieving ltvr");
            }
        }
        done:
        return !(cur_root=ltv); // stash for next go-around
    }

#if 0
    void *resolve(CLL *lnk) { // create and/or fill in ref from scratch, return
        int status=0;
        REF *ref=(REF *) lnk;
        LTI *lti=NULL;
        LTV *ltv;
        LTVR *ltvr;
        STRY(!root,"validating root");
        STRY(!ref,"validating ref");
        STRY(!resolve_keys(ref),"resolving ref keys");
        if (CLL_EMPTY(&ref->root))
            STRY(!LTV_enq(&ref->root,cur_root,HEAD),"enqueueing lti root");

/*
void *ref_get(TOK *ops_tok,int insert,LTV *origin)
{
    int status=0;
    TOK *ref_head=(TOK *) CLL_HEAD(&ops_tok->subtoks);
    TOK *acc_tok=ref_head;

    void *descend(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        int status=0;
        TOK *next_tok=acc_tok; // a little finesse...

        if (*ltv) {
            // need to be able to descend from either TOS or a named node
            if ((*ltv)->flags&LT_CVAR) {
                // Iterate through cvar member hierarchy from here, overwriting ref per iteration
                //STRY(cvar_resolve(),"resolve cvar member");
            }

            if (acc_tok->ref && acc_tok->ref->lti) { // already resolved
                *lti=acc_tok->ref->lti;
            } else {
                LTV *ref_ltv=NULL;
                STRY(!(ref_ltv=LTV_peek(&acc_tok->ltvs,HEAD)),"getting token name");
                int inserted=insert && !((*ltv)->flags&LT_RO) && (!(*ltvr) || !((*ltvr)->flags&LT_RO)); // directive on way in, status on way out
                if ((status=!((*lti)=RBR_find(&(*ltv)->sub.ltis,ref_ltv->data,ref_ltv->len,&inserted))))
                    goto done;
                if (acc_tok->ref) {
                    acc_tok->ref->lti=*lti;
                    LTV_enq(&acc_tok->ref->root,(*ltv),HEAD); // record to be able to free lti if empty
                }
                else
                    STRY(!(acc_tok->ref=REF_new(*lti)),"allocating ref");
            }
        }
        else if (*lti) {
            next_tok=(TOK *) CLL_next(&ops_tok->subtoks,&acc_tok->lnk,FWD); // we're iterating through atom_tok's subtok list
            if (acc_tok->ref && acc_tok->ref->ltvr) { // already resolved? (ltv always accompanies ltvr so no need to check)
                *ltvr=acc_tok->ref->ltvr;
                *ltv=(*ltvr)->ltv;
            } else {
                int reverse=acc_tok->flags&TOK_REV;
                LTV *val_ltv=NULL;
                TOK *subtok=(TOK *) CLL_get(&acc_tok->subtoks,KEEP,HEAD);
                if (subtok) {
                    STRY(!(val_ltv=LTV_peek(&subtok->ltvs,HEAD)),"getting ltvr w/val from token");
                    reverse |= subtok->flags&TOK_REV;
                }
                char *match=val_ltv?val_ltv->data:NULL;
                int matchlen=val_ltv?-1:0;
                if (match)
                    acc_tok->ref->flags|=REF_MATCH;
                (*ltv)=LTV_get(&(*lti)->ltvs,KEEP,reverse,match,matchlen,&(*ltvr)); // lookup

                // check if add is required
                if (!(*ltv) && insert) {
                    if (next_tok && !val_ltv) // insert a null ltv to build hierarchical ref
                        val_ltv=LTV_NULL;
                    if (val_ltv)
                        (*ltv)=LTV_put(&(*lti)->ltvs,val_ltv,reverse,&(*ltvr));
                }

                if (*ltvr && *ltv) {
                    LTV_enq(&acc_tok->ref->ltvs,(*ltv),HEAD); // ensure it's referenced
                    acc_tok->ref->ltvr=*ltvr;
                }
            }
        }
        else if (*ltvr)
            return NULL; // early exit in this case.

        done:
        if (status)
            *flags=LT_TRAVERSE_HALT;

        if (!next_tok) // only advanced by *lti path
            return acc_tok;
        // else
        acc_tok=next_tok;
        return NULL;
    }

    TOK_freerefs(ref_head);
    TOK *ref_tail=(TOK *) listree_traverse(origin,descend,NULL);
    return status?NULL:ref_tail;
}
*/
    }

    void *getnext(CLL *lnk) {
        int status=0;
        REF *ref=(REF *) lnk;

        // FIXME
        return NULL;

        // start from head and reset refs that don't have nexts... return the ref that DOES have a next.
/*
        LTVR *ltvr=NULL;
        if (ref->lti) {
            LTV *ref_ltv=NULL;
            LTI *lti=NULL;
            ref_ltv=LTV_peek(&ref->ltvs,HEAD);
            for (lti=LTI_next(ref->lti); lti && fnmatch_len(ref_ltv->data,ref_ltv->len,lti->name,-1); lti=LTI_next(lti)) {}
            if (lti) {
                TOK_freeref(ref_head);
                ref=ref_head->ref=REF_new(lti);
            }
        }
        if (!ref->lti || (ltvr=(LTVR *) CLL_next(&ref->lti->ltvs,ref->ltvr?&ref->ltvr->lnk:NULL,reverse))) {
            LTV_release(LTV_deq(&ref->ltvs,HEAD));
            LTV_enq(&ref->ltvs,ltvr->ltv,HEAD); // ensure that ltv is referenced by at least one thing so it won't disappear
            ref->ltvr=ltvr;
            return ref_tail;
        }
*/
        STRY(!get(ref),"resolving any unresolved refs");

        done:
        return ref->lti?ref:NULL; // NULL means keep looking
    }
#endif

    int ascend() { return 0; }
    int descend() { return 0; }

    STRY (!refs || !root,"validating arguments");

    REF_dump(stdout,refs);

    REF *ref=NULL;
    STRY(!(ref=REF_HEAD(refs)),"peeking into refs");

    // if root changes, reset refs
    if (root && LTV_peek(&ref->root,HEAD)!=root) {
        CLL_map(refs,FWD,REF_reset);
        LTV_enq(&ref->root,root,HEAD);
    }

    // descend refs

    done:
    return status;
}


int REF_iterate(CLL *refs)
{
    int status=0;

    // ascend refs

    STRY(REF_resolve(refs,NULL),"resolving iterated refs");

    done:
    return status;
}

int REF_assign(CLL *refs,LTV *ltv)
{
    int status=0;
    REF *head=NULL;
    STRY(!refs || !ltv,"validating parameters");
    STRY(!(head=REF_HEAD(refs)),"getting refs head");
    STRY(!head->lti,"validating ref lti");
    STRY(!LTV_put(&head->lti->ltvs,tos,head->reverse,&head->ltvr),"adding ltv to ref");
    done:
    return status;
}
