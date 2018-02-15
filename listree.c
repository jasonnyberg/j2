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
#define __USE_GNU // strndupa, stpcpy

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "listree.h"
#include "reflect.h"

#include "trace.h" // lttng

int show_ref=0;

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
LTI *RBR_find(RBR *rbr,char *name,int len,int insert)
{
    LTI *lti=NULL;
    if (rbr && name) {
        RBN *parent=NULL,**rbn = &rbr->rb_node;
        while (*rbn) {
            int result = strnncmp(name,len,((LTI *) *rbn)->name,-1);
            if (!result) return (LTI *) *rbn; // found it!
            else (parent=*rbn),(rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right);
        }
        if (insert && (lti=LTI_init(NEW(LTI),name,len))) {
            rb_link_node(&lti->rbn,parent,rbn); // add
            rb_insert_color(&lti->rbn,rbr); // rebalance
        }
    }
    return lti;
}

// release old data if present and configure with new data if present
LTV *LTV_renew(LTV *ltv,void *data,int len,LTV_FLAGS flags)
{
    flags|=ltv->flags&LT_META; // need to preserve the original metaflags
    if (ltv->data && (ltv->flags&LT_FREE) && !(ltv->flags&LT_NAP))
        RELEASE(ltv->data);
    ltv->len=(len<0 && !(flags&LT_NSTR))?strlen((char *) data):len;
    ltv->data=data;
    if (flags&LT_DUP) ltv->data=bufdup(ltv->data,ltv->len);
    if (flags&LT_ESC) strstrip(ltv->data,&ltv->len);
    ltv->flags=flags;
    return ltv;
}

// init and prepare for insertions
LTV *LTV_init(LTV *ltv,void *data,int len,LTV_FLAGS flags)
{
    if (ltv && ((flags&LT_NAP) || data)) { // null ptr is error
        ZERO(*ltv);
        if (flags&LT_LIST) CLL_init(&ltv->sub.ltvs);
        LTV_renew(ltv,data,len,flags);
    }
    return ltv;
}

void LTV_free(LTV *ltv)
{
    if (ltv) {
        LTV_renew(ltv,NULL,0,0);
        RELEASE(ltv);
    }
}

int LTV_is_empty(LTV *ltv)
{
    return ltv->flags&LT_LIST? CLL_EMPTY(&ltv->sub.ltvs):RB_EMPTY_ROOT(&ltv->sub.ltis);
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
LTVR *LTVR_init(LTVR *ltvr,LTV *ltv)
{
    if (ltvr && ltv) {
        ZERO(*ltvr);
        CLL_init(&ltvr->lnk);
        ltvr->ltv=ltv;
        ltv->refs++;
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
        RELEASE(ltvr);
    }
    return ltv;
}


// get a new LTI and prepare for insertion
LTI *LTI_init(LTI *lti,char *name,int len)
{
    if (lti && name) {
        ZERO(*lti);
        lti->name=bufdup(name,len);
        CLL_init(&lti->ltvs);
    }
    return lti;
}

void LTI_free(LTI *lti)
{
    if (lti) {
        RELEASE(lti->name);
        RELEASE(lti);
    }
}


//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && !(ltv->refs) && !(ltv->flags&LT_RO)) {
        if (ltv->flags&LT_REFS)      REF_delete(ltv); // cleans out REFS
        else if (ltv->flags&LT_LIST) CLL_release(&ltv->sub.ltvs,LTVR_release);
        else                         RBR_release(&ltv->sub.ltis,LTI_release);
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

// add to preop to avoid repeat visits in listree traverse
void *listree_acyclic(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
    if ((*flags&LT_TRAVERSE_LTV) && (*ltv)->flags&LT_AVIS)
        *flags|=LT_TRAVERSE_HALT;
    return NULL;
}

void *listree_traverse(CLL *ltvs,LTOBJ_OP preop,LTOBJ_OP postop)
{
    int depth=0,cleanup=0;
    void *rval=NULL;

    void *descend_ltv(LTVR *parent,LTV *ltv) {
        void *descend_ltvr(CLL *lnk) { // for list-form ltv
            LTVR *ltvr=(LTVR *) lnk;
            if (!ltvr) goto done;

            LTV *parent=ltv; LTI *null=NULL;
            LT_TRAVERSE_FLAGS flags=LT_TRAVERSE_LTV_LTVR;
            if (cleanup && ltvr->ltv) descend_ltv(ltvr,ltvr->ltv);
            else if (preop && (rval=preop(&null,&ltvr,&parent,depth,&flags)) ||
                     ((flags&LT_TRAVERSE_HALT) || (rval=descend_ltv(ltvr,ltvr->ltv))) ||
                     postop && ((flags|=LT_TRAVERSE_POST),(rval=postop(&null,&ltvr,&parent,depth,&flags))))
                goto done;
        done:
            return rval;
        }

        void *descend_lti(RBN *rbn) {
            LTI *lti=(LTI *) rbn;

            void *descend_ltvr(CLL *lnk) { // for normal-form (ltv->lti->ltvr) form ltv
                LTVR *ltvr=(LTVR *) lnk;
                if (!ltvr) goto done;

                LTI *parent=lti; LTV *child=NULL;
                LT_TRAVERSE_FLAGS flags=LT_TRAVERSE_LTI_LTVR;
                if (cleanup && ltvr->ltv) descend_ltv(ltvr,ltvr->ltv);
                else if (preop && (rval=preop(&parent,&ltvr,&child,depth,&flags)) ||
                         ((flags&LT_TRAVERSE_HALT) || (rval=descend_ltv(ltvr,ltvr->ltv))) ||
                         postop && ((flags|=LT_TRAVERSE_POST),(rval=postop(&parent,&ltvr,&child,depth,&flags))))
                    goto done;
            done:
                return rval;
            }

            if (!lti) goto done;
            LTV *parent=ltv; LTVR *child=NULL;
            LT_TRAVERSE_FLAGS flags=LT_TRAVERSE_LTI;
            if (cleanup) CLL_map(&lti->ltvs,FWD,descend_ltvr);
            else if (preop && (rval=preop(&lti,&child,&parent,depth,&flags)) ||
                     ((flags&LT_TRAVERSE_HALT) || (rval=child?descend_ltvr(&child->lnk):CLL_map(&lti->ltvs,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,descend_ltvr))) ||
                     postop && ((flags|=LT_TRAVERSE_POST),(rval=postop(&lti,&child,&parent,depth,&flags))))
                goto done;
        done:
            return rval;
        }

        if (!ltv) goto done;
        LTI *child=NULL;
        LT_TRAVERSE_FLAGS flags=LT_TRAVERSE_LTV;
        if (cleanup) // only descends (and cleans up) LTVs w/absolute visited flag
            return (ltv->flags&LT_AVIS && !((ltv->flags&=~LT_AVIS)&LT_AVIS) && !(ltv->flags&LT_REFS))? LTV_map(ltv,FWD,descend_lti,descend_ltvr):NULL;
        else if (!(ltv->flags&LT_RVIS)) {
            if (preop && (rval=preop(&child,&parent,&ltv,depth,&flags))) goto done;
            if ((flags&LT_TRAVERSE_HALT) || (ltv->flags&LT_REFS)) goto done;
            ltv->flags|=LT_RVIS;
            depth++;
            rval=child?descend_lti(&child->rbn):LTV_map(ltv,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,descend_lti,descend_ltvr);
            depth--;
            ltv->flags&=~LT_RVIS;
            if (rval) goto done;

            if (postop && ((flags|=LT_TRAVERSE_POST),(rval=postop(&child,&parent,&ltv,depth,&flags)))) goto done;
        }

    done:
        if (ltv) ltv->flags|=LT_AVIS;
        return rval;
    }

    void *traverse(CLL *lnk) { LTVR *ltvr=(LTVR *) lnk; return descend_ltv(ltvr,ltvr->ltv); }
    rval=CLL_map(ltvs,FWD,traverse);
    cleanup=1;
    preop=postop=NULL;
    CLL_map(ltvs,FWD,traverse); // clean up "visited" flags
    return rval;
}

void *ltv_traverse(LTV *ltv,LTOBJ_OP preop,LTOBJ_OP postop)
{
    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,ltv,HEAD);
    void *result=listree_traverse(&ltvs,preop,postop);
    LTV_deq(&ltvs,HEAD);
    return result;
}

//////////////////////////////////////////////////
// Basic LT insert/remove
//////////////////////////////////////////////////

extern LTI *LTI_first(LTV *ltv) { return (!ltv || (ltv->flags&LT_LIST))?NULL:(LTI *) rb_first(&ltv->sub.ltis); }
extern LTI *LTI_last(LTV *ltv)  { return (!ltv || (ltv->flags&LT_LIST))?NULL:(LTI *) rb_last(&ltv->sub.ltis); }

extern LTI *LTI_next(LTI *lti) { return lti? (LTI *) rb_next((RBN *) lti):NULL; }
extern LTI *LTI_prev(LTI *lti) { return lti? (LTI *) rb_prev((RBN *) lti):NULL; }

LTI *LTI_lookup(LTV *ltv,LTV *name,int insert)
{
    LTI *lti=NULL;
    if (LTV_wildcard(name))
        for (lti=LTI_first(ltv); lti && fnmatch_len(name->data,name->len,lti->name,-1); lti=LTI_next(lti));
    else
        lti=RBR_find(&ltv->sub.ltis,name->data,name->len,insert);
    done:
    return lti;
}

LTI *LTI_resolve(LTV *ltv,char *name,int insert)
{
    LTV *nameltv=LTV_init(NEW(LTV),name,-1,LT_NOWC);
    LTI *lti=LTI_lookup(ltv,nameltv,insert);
    LTV_free(nameltv);
    return lti;
}

int LTV_empty(LTV *ltv)
{
    if (!ltv) return true;
    else if (ltv->flags&LT_LIST) return CLL_EMPTY(&ltv->sub.ltvs);
    else return LTI_first(ltv)==NULL;
}

LTV *LTV_put(CLL *ltvs,LTV *ltv,int end,LTVR **ltvr_ret)
{
    int status=0;
    LTVR *ltvr=NULL;
    if (ltv && ltvs && (ltvr=LTVR_init(NEW(LTVR),ltv))) {
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
        if (!ltvr || !ltvr->ltv || ltvr->ltv->flags&LT_NAP || fnmatch_len(ltvr->ltv->data,ltvr->ltv->len,match->data,match->len)) return NULL;
        else return lnk;
    }

    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (!(ltvr=(LTVR *) match?
            CLL_mapfrom(ltvs,((ltvr_ret && (*ltvr_ret))?&(*ltvr_ret)->lnk:NULL),dir,ltv_match):
            CLL_next(ltvs,(ltvr_ret && (*ltvr_ret))?&(*ltvr_ret)->lnk:NULL,dir)))
        goto done;
    ltv=ltvr->ltv;
    if (pop) {
        CLL_cut(&ltvr->lnk);
        LTVR_free(ltvr);
        ltvr=NULL;
    }
    done:
    if (ltvr_ret) (*ltvr_ret)=ltvr;
    return ltv;
}

// delete an lti from an ltv
void LTV_erase(LTV *ltv,LTI *lti) { RBN_release(&ltv->sub.ltis,&lti->rbn,LTI_release); }

LTV *LTV_dup(LTV *ltv)
{
    if (!ltv) return NULL;

    int flags=ltv->flags & ~LT_NDUP;
    if (!(flags&LT_NAP))
        flags |= LT_DUP;
    return LTV_init(NEW(LTV),ltv->data,ltv->len,flags);
}

LTV *LTV_concat(LTV *a,LTV *b)
{
    int status=0;
    STRY(!a || !b,"validating args");
    char *buf=NULL;
    LTV *ltv=NULL;
    STRY(!(buf=mymalloc(a->len+b->len)),"validating buf allocation");
    STRY(!(ltv=LTV_init(NEW(LTV),buf,a->len+b->len,LT_OWN)),"validating ltv allocation");
    strncpy(buf,a->data,a->len);
    strncpy(buf+a->len,b->data,b->len);
 done:
    return status?NULL:ltv;
}

int LTV_wildcard(LTV *ltv)
{
    if (ltv->flags&LT_NOWC)
        return false;
    int tlen=series(ltv->data,ltv->len,NULL,"*?",NULL);
    return tlen < ltv->len;
}

int LTI_hide(LTI *lti)
{
    int len=strlen(lti->name);
    return series(lti->name,len,NULL," ",NULL)<len && !show_ref;
}

int LTV_hide(LTV *ltv) { return !show_ref && ((ltv->flags&LT_TYPE)); }

void print_ltvs(FILE *ofile,char *pre,CLL *ltvs,char *post,int maxdepth)
{
    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        switch (*flags&LT_TRAVERSE_TYPE) {
            case LT_TRAVERSE_LTI:
                if (maxdepth && depth>=maxdepth || LTI_hide(*lti))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    fprintf(ofile,"%*c\"%s\"\n",MAX(0,depth*4-2),' ',(*lti)->name);
                break;
            case LT_TRAVERSE_LTV:
                if (pre) fprintf(ofile,"%*c%s",depth*4,' ',pre);
                else fprintf(ofile,"%*c[",depth*4,' ');
                if      ((*ltv)->flags&LT_REFS) { REF_printall(ofile,(*ltv),"REFS:\n"); fstrnprint(ofile,(*ltv)->data,(*ltv)->len); }
                else if ((*ltv)->flags&LT_CVAR) cif_print_cvar(ofile,(*ltv),depth);
                else if ((*ltv)->flags&LT_IMM)  fprintf(ofile,"IMM 0x%x",(*ltv)->data);
                else if ((*ltv)->flags&LT_NULL) fprintf(ofile,"<null>");
                else if ((*ltv)->flags&LT_BIN)  hexdump(ofile,(*ltv)->data,(*ltv)->len);
                else                            fstrnprint(ofile,(*ltv)->data,(*ltv)->len);
                if (post) fprintf(ofile,"%s",post);
                else fprintf(ofile,"]\n");

                if (LTV_hide(*ltv))
                    *flags|=LT_TRAVERSE_HALT;
                break;
            default:
                break;
        }
        return NULL;
    }

    if (ltvs)
        listree_traverse(ltvs,preop,NULL);
    else
        fprintf(ofile,"NULL");
}

void print_ltv(FILE *ofile,char *pre,LTV *ltv,char *post,int maxdepth)
{
    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,ltv,HEAD);
    print_ltvs(ofile,pre,&ltvs,post,maxdepth);
    LTV_deq(&ltvs,HEAD);
}


void ltvs2dot(FILE *ofile,CLL *ltvs,int maxdepth,char *label) {
    int i=0;

    void *lnk2dot(CLL *lnk) {
        fprintf(ofile,"\"%x\" [label=\"\" shape=point color=brown penwidth=2.0]\n",lnk);
        if (lnk->lnk[TAIL]!=lnk)
            fprintf(ofile,"\"%x\" -> \"%x\" [color=brown penwidth=2.0]\n",lnk->lnk[TAIL],lnk);
        return NULL;
    }

    void *ltvr2dot(CLL *lnk) {
        LTVR *ltvr=(LTVR *) lnk;
        fprintf(ofile,"\"%x\" -> \"LTV%x\" [color=purple len=0.1]\n",lnk,ltvr->ltv);
        return lnk2dot(lnk);
    }

    void cll2dot(CLL *cll,char *label) {
        if (label)
            fprintf(ofile,"\"%1$s_%2$x\" [label=\"%1$s\" shape=ellipse style=filled fillcolor=gray]\n\"%1$s_%2$x\" -> \"%2$x\"\n",label,cll);
        lnk2dot(cll);
    }

    void lti2dot(LTV *ltv,LTI *lti) {
        fprintf(ofile,"\"LTI%x\" [label=\"%s\" shape=ellipse]\n",&lti->rbn,lti->name);
        if (rb_parent(&lti->rbn)) fprintf(ofile,"\"LTI%x\" -> \"LTI%x\" [color=blue]\n",rb_parent(&lti->rbn),&lti->rbn);
        fprintf(ofile,"\"LTI%x\" -> \"%x\"\n",&lti->rbn,&lti->ltvs);
        cll2dot(&lti->ltvs,NULL);
    }

    void lti_ltvr2dot(LTI *lti,LTVR *ltvr) { ltvr2dot(&ltvr->lnk); }
    void ltv_ltvr2dot(LTV *ltv,LTVR *ltvr) { ltvr2dot(&ltvr->lnk); }

    void ltv_descend(LTV *ltv) {
        if (ltv->flags&LT_LIST) {
            if (ltv->flags&LT_REFS)
                REF_dot(ofile,ltv,"REFS");
            fprintf(ofile,"\"LTV%x\" -> \"%x\" [color=blue]\n",ltv,&ltv->sub.ltvs);
            //  cll2dot(&ltv->sub.ltvs,NULL);
        }
        else if (ltv->sub.ltis.rb_node) {
            fprintf(ofile,"subgraph cluster_%d { subgraph { /*rank=same*/\n",i++);
            for (LTI *lti=LTI_first(ltv);lti;lti=LTI_next(lti))
                fprintf(ofile,"\"LTI%x\"\n",&lti->rbn);
            fprintf(ofile,"}}\n");
            fprintf(ofile,"\"LTV%x\" -> \"LTI%x\" [color=blue]\n",ltv,ltv->sub.ltis.rb_node);
        }
    }

    void ltv2dot(LTVR *ltvr,LTV *ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        char *color=ltv->flags&LT_RO?"red":"black";
        if (ltv->len && !(ltv->flags&LT_NSTR)) {
            fprintf(ofile,"\"LTV%x\" [shape=box style=filled fillcolor=lightsteelblue color=%s label=\"",ltv,color);
            fstrnprint(ofile,ltv->data,ltv->len);
            fprintf(ofile,"\"]\n");
        }
        else if (ltv->flags&LT_REFS)
            fprintf(ofile,"\"LTV%x\" [label=\"REFS(%x)\" shape=box style=filled fillcolor=yellow color=%s]\n",ltv,ltv->data,color),
                REF_dot(ofile,ltv,"REFS");
        else if (ltv->flags&LT_CVAR)
            fprintf(ofile,"\"LTV%x\" [label=\"CVAR(%x)\" shape=box style=filled fillcolor=yellow color=%s]\n",ltv,ltv->data,color),
                cif_dot_cvar(ofile,ltv); // invoke reflection
        else if (ltv->flags&LT_IMM)
            fprintf(ofile,"\"LTV%x\" [label=\"%x (imm)\" shape=box style=filled fillcolor=gold color=%s]\n",ltv,ltv->data,color);
        else if (ltv->flags&LT_NULL)
            fprintf(ofile,"\"LTV%x\" [label=\"\" shape=point style=filled fillcolor=purple fillcolor=pink color=%s]\n",ltv,color);
        else
            fprintf(ofile,"\"LTV%x\" [label=\"\" shape=box style=filled height=.1 width=.3 fillcolor=gray color=%s]\n",ltv,color);
    }

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        switch(*flags) {
            case LT_TRAVERSE_LTI:
                if (maxdepth && depth>=maxdepth || LTI_hide(*lti))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    lti2dot(*ltv,*lti);
                break;
            case LT_TRAVERSE_LTI_LTVR:
                lti_ltvr2dot(*lti,*ltvr);
                break;
            case LT_TRAVERSE_LTV_LTVR:
                ltv_ltvr2dot(*ltv,*ltvr);
                break;
            case LT_TRAVERSE_LTV:
                ltv2dot(*ltvr,*ltv,depth,flags);
                if (LTV_hide(*ltv))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    ltv_descend(*ltv);
                break;
            default: // finesse: if LT_TRAVERSE_HALT is set by listree_acyclic, none of the above will hit
                break;
        }
        return NULL;
    }

    cll2dot(ltvs,label);
    CLL_map(ltvs,FWD,ltvr2dot);
    listree_traverse(ltvs,preop,NULL);
}


void ltvs2dot_simple(FILE *ofile,CLL *ltvs,int maxdepth,char *label) {
    int i=0;

    void lti2dot(LTV *ltv,LTI *lti) {
        fprintf(ofile,"\"LTI%x\" [label=\"%s\" shape=ellipse color=red4]\n",lti,lti->name);
        fprintf(ofile,"\"LTV%x\" -> \"LTI%x\" [color=blue]\n",ltv,lti);
    }

    void lti_ltvr2dot(LTI *lti,LTVR *ltvr) { fprintf(ofile,"\"LTI%x\" -> \"LTV%x\" [color=purple]\n",lti,ltvr->ltv); }
    void ltv_ltvr2dot(LTV *ltv,LTVR *ltvr) { fprintf(ofile,"\"LTV%x\" -> \"LTV%x\" [color=red]\n",ltv,ltvr->ltv); }

    void ltv_descend(LTV *ltv) {
        if (!(ltv->flags&LT_LIST) && ltv->sub.ltis.rb_node) {
            fprintf(ofile,"subgraph cluster_%d { subgraph { /*rank=same*/\n",i++);
            for (LTI *lti=LTI_first(ltv);lti;lti=LTI_next(lti))
                fprintf(ofile,"\"LTI%x\"\n",&lti->rbn);
            fprintf(ofile,"}}\n");
        }
    }

    void ltv2dot(LTVR *ltvr,LTV *ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        if (ltv->len && !(ltv->flags&LT_NSTR)) {
            fprintf(ofile,"\"LTV%x\" [shape=box style=filled fillcolor=tan label=\"",ltv);
            fstrnprint(ofile,ltv->data,ltv->len);
            fprintf(ofile,"\"]\n");
        }
        else if (ltv->flags&LT_REFS)
            fprintf(ofile,"\"LTV%x\" [label=\"REFS(%x)\" shape=box style=filled]\n",ltv,ltv->data),
                REF_dot(ofile,ltv,"REFS");
        else if (ltv->flags&LT_CVAR)
            fprintf(ofile,"\"LTV%x\" [label=\"CVAR(%x)\" shape=box style=filled]\n",ltv,ltv->data),
                cif_dot_cvar(ofile,ltv); // invoke reflection
        else if (ltv->flags&LT_IMM)
            fprintf(ofile,"\"LTV%x\" [label=\"I(%x)\" shape=box style=filled]\n",ltv,ltv->data);
        else if (ltv->flags==LT_NULL)
            fprintf(ofile,"\"LTV%x\" [label=\"\" shape=point style=filled color=purple]\n",ltv);
        else
            fprintf(ofile,"\"LTV%x\" [label=\"\" shape=box style=filled height=.1 width=.3]\n",ltv);
    }

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        switch(*flags) {
            case LT_TRAVERSE_LTI:
                if (maxdepth && depth>=maxdepth || LTI_hide(*lti))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    lti2dot(*ltv,*lti);
                break;
            case LT_TRAVERSE_LTI_LTVR:
                lti_ltvr2dot(*lti,*ltvr);
                break;
            case LT_TRAVERSE_LTV_LTVR:
                ltv_ltvr2dot(*ltv,*ltvr);
                break;
            case LT_TRAVERSE_LTV:
                ltv2dot(*ltvr,*ltv,depth,flags);
                if (LTV_hide(*ltv))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    ltv_descend(*ltv);
                break;
            default: // finesse: if LT_TRAVERSE_HALT is set by listree_acyclic, none of the above will hit
                break;
        }
        return NULL;
    }

    listree_traverse(ltvs,preop,NULL);
}


void graph_ltvs(FILE *ofile,CLL *ltvs,int maxdepth,char *label) {
    fprintf(ofile,"digraph iftree\n{\ngraph [rankdir=\"LR\" /*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");
    ltvs2dot_simple(ofile,ltvs,maxdepth,label);
    fprintf(ofile,"}\n");
}

void graph_ltvs_to_file(char *filename,CLL *ltvs,int maxdepth,char *label) {
    FILE *ofile=fopen(filename,"w");
    graph_ltvs(ofile,ltvs,maxdepth,label);
    fclose(ofile);
}

void graph_ltv_to_file(char *filename,LTV *ltv,int maxdepth,char *label) {
    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,ltv,HEAD);
    graph_ltvs_to_file(filename,&ltvs,maxdepth,label);
    LTV_deq(&ltvs,HEAD);
}

//////////////////////////////////////////////////
//////////////////////////////////////////////////

CLL *LTV_list(LTV *ltv)
{
    return (ltv && ltv->flags&LT_LIST)? &ltv->sub.ltvs:NULL;
}

LTV *LT_put(LTV *parent,char *name,int end,LTV *child)
{
    if (parent && name && child) {
        LTI *lti=LTI_resolve(parent,name,true);
        return lti?LTV_enq(&lti->ltvs,child,end):NULL;
    }
    else
        return NULL;
}

LTV *LT_get(LTV *parent,char *name,int end,int pop)
{
    if (parent && name) {
        LTI *lti=LTI_resolve(parent,name,false);
        return lti?(pop?LTV_deq(&lti->ltvs,end):LTV_peek(&lti->ltvs,end)):NULL;
    }
    else
        return NULL;
}


//////////////////////////////////////////////////
// REF
// special case; client creates ref, which acts as sentinel
//////////////////////////////////////////////////

int ref_count=0;

REF *refpush(CLL *cll,REF *ref) { return (REF *) CLL_put(cll,&ref->lnk,HEAD); }
REF *refpop(CLL *cll)           { return (REF *) CLL_get(cll,POP,HEAD); }

LTV *REF_root(REF *ref) { return ref?LTV_peek(&ref->root,HEAD):NULL; }

REF *REF_init(REF *ref,char *data,int len)
{
    int quote=(len>1 && data[0]=='\'' && data[len-1]=='\'');
    if (quote) {
        data+=1;
        len-=2;
    }
    int rev=data[0]=='-';
    if (len-rev==0)
        return NULL;

    if (ref && CLL_init(&ref->lnk))
    {
        CLL_init(&ref->keys);
        LTV_enq(&ref->keys,LTV_init(NEW(LTV),data+rev,len-rev,LT_DUP|(quote?LT_NOWC:LT_ESC)),HEAD);
        CLL_init(&ref->root);
        ref->lti=NULL;
        ref->ltvr=NULL;
        ref->cvar=NULL;
        ref->reverse=rev;
        ref_count++;
    }
    return ref;
}

LTV *REF_reset(REF *ref,LTV *newroot)
{
    int status=0;
    LTV *root=REF_root(ref);
    if (root==newroot)
        goto done;
    if (root && ref->lti) {
        void *prune_placeholders(CLL *lnk) {
            LTVR *ltvr=(LTVR *) lnk;
            if (ltvr->ltv->flags==LT_NULL && LTV_empty(ltvr->ltv))
                LTVR_release(lnk);
        }
        CLL_map(&ref->lti->ltvs,FWD,prune_placeholders);
        if (CLL_EMPTY(&ref->lti->ltvs)) // if LTI is pruneable
            RBN_release(&root->sub.ltis,&ref->lti->rbn,LTI_release); // prune it
    }
    ref->lti=NULL;
    ref->ltvr=NULL;
    LTV_release(ref->cvar);
    CLL_release(&ref->root,LTVR_release);
    if (newroot)
        LTV_enq(&ref->root,newroot,HEAD);
    done:
    return newroot;
}

void REF_free(CLL *lnk)
{
    if (!lnk) return;
    REF *ref=(REF *) lnk;
    CLL_cut(&ref->lnk); // take it out of any list it's in
    REF_reset(ref,NULL);
    CLL_release(&ref->keys,LTVR_release);
    RELEASE(ref);
    ref_count--;
}

///////////////////////////////////////////////////////

LTV *REF_create(LTV *refs)
{
    int status=0;
    STRY(!refs,"validating params");
    if (!(refs->flags&LT_REFS)) { // promote an ltv to a ref
        STRY(!LTV_is_empty(refs),"promoting non-empty ltv to ref");
        STRY(refs->flags&(LT_CVAR|LT_NAP|LT_NSTR|LT_REFL),"promoting incomptible ltv to ref");
        refs->flags|=(LT_REFS|LT_LIST);
        CLL_init(&refs->sub.ltvs);
    }
    char *data=refs->data;
    int len=refs->len;
    CLL *cll=LTV_list(refs);
    STRY(REF_delete(refs),"clearing any cll");

    int advance(int bump) { bump=MIN(bump,len); data+=bump; len-=bump; return bump; }

    int quote() { return series(data,len,NULL,NULL,"''"); }
    int name()  { return series(data,len,NULL,".[",NULL); }
    int val()   { return series(data,len,NULL,NULL,"[]"); } // val=data+1,len-2!!!
    int sep()   { return series(data,len,".",NULL,NULL);  }

    unsigned tlen,striplen;
    REF *ref=NULL;

    while (len) { // parse ref keys
        STRY(!((tlen=quote()) || (tlen=name())),"parsing ref name"); // mandatory
        STRY(!(ref=REF_init(NEW(REF),data,tlen)),"allocating name ref");
        STRY(!CLL_put(cll,&ref->lnk,HEAD),"enqueing name ref");
        advance(tlen);

        while ((tlen=val())) { // parse vals (optional)
            STRY(!LTV_enq(&ref->keys,LTV_init(NEW(LTV),data+1,tlen-2,0),TAIL),"enqueueing val key");
            advance(tlen);
        }

        if (len) // if there's anything left, it has to be a single separator
            STRY(advance(sep())!=1,"parsing sep");
    }

    done:
    return status?NULL:refs;
}

int REF_delete(LTV *refs)
{
    int status=0;
    STRY(!refs || !(refs->flags&LT_REFS),"validating params");
    CLL *cll=LTV_list(refs);
    void release(CLL *lnk) { REF_free(lnk); }
    CLL_release(cll,release);
 done:
    return status;
}

int REF_resolve(LTV *root,LTV *refs,int insert)
{
    int status=0;
    STRY(!refs || !(refs->flags&LT_REFS),"validating params");
    CLL *cll=LTV_list(refs);
    REF *ref=NULL;
    int placeholder=0;

    void *resolve(CLL *lnk) {
        int status=0;
        ref=(REF *) lnk;
        placeholder=0;
        LTV *name=NULL;
        LTVR *ltvr=NULL;

        STRY(!(name=LTV_get(&ref->keys,KEEP,HEAD,NULL,&ltvr)),"validating name key"); // name is first key (use get for ltvr)
        LTVR *val=(LTVR *) CLL_next(&ref->keys,&ltvr->lnk,FWD); // val will be next key

        STRY(!root,"validating root");
        root=REF_reset(ref,root); // clean up ref if root changed

        char *buf=NULL;
        if (root->flags&LT_CVAR && (ref->cvar=cif_create_cvar(LT_get(root,TYPE_BASE,HEAD,KEEP),root->data,PRINTA(buf,name->len,name->data))))
            root=ref->cvar;
        else {
            if (!ref->lti) { // resolve lti
                if ((status=!(ref->lti=LTI_lookup(root,name,insert))))
                    goto done; // return failure, but don't log it
            }
            if (!ref->ltvr) { // resolve ltv(r)
                TRY(!LTV_get(&ref->lti->ltvs,KEEP,ref->reverse,val?val->ltv:NULL,&ref->ltvr),"retrieving ltvr");
                CATCH(!ref->ltvr && insert,0,goto install_placeholder,"retrieving ltvr, installing placeholder");
                if (status) // found LTI but no matching LTV
                    goto done;
            }
            root=ref->ltvr->ltv;
        }
        goto done; // success!

    install_placeholder:
        STRY(!(root=LTV_put(&ref->lti->ltvs,(placeholder=!val)?LTV_NULL:LTV_dup(val->ltv),ref->reverse,&ref->ltvr)),"inserting placeholder ltvr");

    done:
        return status?NON_NULL:NULL;
    }

    STRY(!cll,"validating refs");
    if (!root)
        STRY(!(root=REF_root(REF_TAIL(refs))),"validating root");
    status=(CLL_map(cll,REV,resolve)!=NULL);
    if (placeholder) { // remove terminal placeholder
        LTVR_release(&ref->ltvr->lnk);
        ref->ltvr=NULL;
    }
    done:
    return status;
}

int REF_iterate(LTV *refs,int pop)
{
    int status=0;
    STRY(!refs || !(refs->flags&LT_REFS),"validating params");
    CLL *cll=LTV_list(refs);

    void *iterate(CLL *lnk) { // return null if there is no next
        REF *ref=(REF *) lnk;

        LTVR *name_ltvr=NULL,*ref_ltvr=ref->ltvr;
        LTV *name=LTV_get(&ref->keys,KEEP,HEAD,NULL,&name_ltvr);
        LTVR *val=(LTVR *) CLL_next(&ref->keys,&name_ltvr->lnk,FWD); // val will be next key

        if (ref->cvar && 0/*iterate cvar*/) {
        } else {
            if (!ref->lti || !ref->ltvr)
                goto done;
            LTV *next_ltv=LTV_get(&ref->lti->ltvs,KEEP,ref->reverse,val?val->ltv:NULL,&ref->ltvr);
            if (pop)
                LTVR_release(&ref_ltvr->lnk);
            if (next_ltv)
                return ref;

            if (LTV_wildcard(name)) {
                LTV *root=REF_root(ref);
                LTI *lti=ref->lti;
                for (ref->lti=LTI_next(ref->lti); ref->lti && fnmatch_len(name->data,name->len,ref->lti->name,-1); ref->lti=LTI_next(ref->lti)); // find next lti
                if (CLL_EMPTY(&lti->ltvs)) // if LTI is pruneable
                    RBN_release(&root->sub.ltis,&lti->rbn,LTI_release); // prune it
                if (ref->lti!=NULL)
                    return ref;
            }
        }

        REF_reset(ref,NULL);

        done:
        return NULL;
    }

    STRY(!cll,"validating arguments");
    if (CLL_map(cll,FWD,iterate))
        STRY(REF_resolve(NULL,refs,false),"resolving iterated ref");

    done:
    return status;
}

int REF_assign(REF *ref,LTV *ltv)
{
    int status=0;
    LTV *ref_ltv=REF_ltv(ref);
    if (ref_ltv && (ref_ltv->flags&LT_CVAR)) {
        LTV *addr_type=cif_isaddr(ref_ltv);
        if (addr_type) {
            LTV *newltv=NULL;
            STRY(!(newltv=cif_coerce_i2c(ltv,addr_type)),"coercing ltv");
            LTVR_release(&ref->ltvr->lnk);
            STRY(!LTV_put(&ref->lti->ltvs,newltv,ref->reverse,&ref->ltvr),"adding coerced ltv to ref");
        } else {
            STRY(!cif_assign_cvar(ref_ltv,ltv),"assigning to cvar");
        }
    } else {
        STRY(!ref->lti,"validating ref lti");
        STRY(!LTV_put(&ref->lti->ltvs,ltv,ref->reverse,&ref->ltvr),"adding ltv to ref");
    }
    done:
    return status;
}

int REF_remove(REF *ref)
{
    int status=0;
    STRY(!ref->lti || !ref->ltvr,"validating ref lti, ltvr");
    LTVR_release(&ref->ltvr->lnk);
    ref->ltvr=NULL;
    done:
    return status;
}

LTV *REF_key(REF *ref)   { return LTV_peek(&ref->keys,HEAD); }
LTI *REF_lti(REF *ref)   { return ref?ref->lti: NULL; }
LTVR *REF_ltvr(REF *ref) { return ref?ref->ltvr:NULL; }
LTV *REF_ltv(REF *ref)   {
    if (!ref)
        return NULL;
    else if (ref->cvar)
        return ref->cvar;
    else if (ref->ltvr)
        return ref->ltvr->ltv;
    else return NULL;
}

void REF_print(FILE *ofile,REF *ref,char *label)
{
    fprintf(ofile,label);
    print_ltvs(ofile,"root(",&ref->root,")",1);
    print_ltvs(ofile,"key(",&ref->keys,")",1);
    fprintf(ofile,"lti(%x)",ref->lti);
    print_ltv(ofile,"ltv(",ref->ltvr?ref->ltvr->ltv:NULL,")",1);
    fprintf(ofile,"\n");
}

void REF_printall(FILE *ofile,LTV *refs,char *label)
{
    if (!refs || !(refs->flags&LT_REFS))
        return;
    CLL *cll=LTV_list(refs);
    void *dump(CLL *lnk) { REF_print(ofile,(REF *) lnk,""); return NULL; }
    fprintf(ofile,label);
    CLL_map(cll,REV,dump);
}

void REF_dot(FILE *ofile,LTV *refs,char *label)
{
    if (!refs || !(refs->flags&LT_REFS))
        return;
    CLL *cll=LTV_list(refs);
    void *op(CLL *lnk) {
        REF *ref=(REF *) lnk;
        fprintf(ofile,"\"%x\" [label=\"\" shape=box label=\"",ref);
        fprintf(ofile,"%s",ref->reverse?"REV":"FWD");
        fprintf(ofile,"\"]\n");
        fprintf(ofile,"\"%x\" -> \"%x\" [color=red penwidth=2.0]\n",ref,lnk->lnk[0]);
        //fprintf(ofile,"\"%x\" -> \"%x\"\n",tok,&tok->ltvs);
        fprintf(ofile,"\"%2$x\" [label=\"root\"]\n\"%1$x\" -> \"%2$x\"\n",ref,&ref->root);
        ltvs2dot(ofile,&ref->root,0,NULL);
        fprintf(ofile,"\"%2$x\" [label=\"keys\"]\n\"%1$x\" -> \"%2$x\"\n",ref,&ref->keys);
        ltvs2dot(ofile,&ref->keys,0,NULL);
    }

    fprintf(ofile,"\"%x\" [label=\"%s\"]\n",cll,label);
    fprintf(ofile,"\"%x\" -> \"%x\" [color=green]\n",cll,cll->lnk[0]);
    CLL_map(cll,FWD,op);
}
