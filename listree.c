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
#include "compile.h"

#include "trace.h" // lttng

int show_ref=0;
int lti_count=0,ltvr_count=0,ltv_count=0;

//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

// homebrew implementation of Arne Adersson's BST
// http://user.it.uu.se/~arnea/ps/simp.pdf

static LTI aa_sentinel={.lnk={&aa_sentinel,&aa_sentinel},.level=0};

int LTI_invalid(LTI *lti) { return lti==NULL || lti==&aa_sentinel; }

static void aa_rot(LTI **t,int dir) {
    LTI *temp=(*t);
    (*t)=(*t)->lnk[!dir];
    temp->lnk[!dir]=(*t)->lnk[dir];
    (*t)->lnk[dir]=temp;
}

static void aa_skew(LTI **t) {
    if ((*t)->lnk[LEFT]->level==(*t)->level)
        aa_rot(t,RIGHT);
}

static void aa_split(LTI **t) {
    if ((*t)->lnk[RIGHT]->lnk[RIGHT]->level==(*t)->level) {
        aa_rot(t,LEFT);
        (*t)->level++;
    }
}

static LTI *aa_most(LTI *t,int dir) {
    if (t==&aa_sentinel)
        return t;
    LTI *lti=aa_most(t->lnk[dir],dir);
    return lti==&aa_sentinel?t:lti;
}

static LTI *aa_find(LTI **t,char *name,int len,int *insert) { // find/insert node into t
    LTI *lti=NULL;
    int iter=(*insert)&ITER;
    int dir=(*insert)&1;
    if ((*t)==&aa_sentinel)
        lti=((*insert)&INSERT)?(*t)=LTI_init(NEW(LTI),name,len):NULL;
    else {
        int delta=0;
        for (int i=0;delta==0 && i<PREVIEWLEN && i<len && i<(*t)->len;i++)
            delta=name[i]-(*t)->preview[i];
        if (!delta) // preview matched, compare full strings
            delta=strnncmp(name,len,(*t)->name,(*t)->len);
        int deltadir=delta<0?LEFT:RIGHT; // turn LTZ/Z/GTZ into left/right/right
        if (delta) {
            if (LTI_invalid(lti=aa_find(&(*t)->lnk[deltadir],name,len,insert)) && iter && dir!=deltadir)
                lti=*t;
        }
        else if (iter) // matches, but find next smaller/larger
            lti=aa_most((*t)->lnk[dir],!dir);
        else // return match
            lti=(*t),(*insert)=0;
    }
    if ((*insert)&INSERT) { // rebalance if necessary
        aa_skew(t);
        aa_split(t);
    }
    return lti;
}


static LTI *aa_remove(LTI **t,char *name,int len,LTI **todelete,LTI **tokeep) { // must be called with todelete/tokeep=&aa_sentinel
    int status=0;
    LTI *remove=NULL;

    TRYCATCH(LTI_invalid(*t),0,done,"checking for no such node");

    inline LTI **save(LTI **dest,LTI *src) { (*dest)=src; return dest; }

    // descend tree, finding matching node ("todelete") and then "next-greater" leaf ("tokeep")
    int delta=0;
    for (int i=0;delta==0 && i<PREVIEWLEN && i<len && i<(*t)->len;i++)
        delta=name[i]-(*t)->preview[i];
    if (!delta) // preview matched, compare full strings
        delta=strnncmp(name,len,(*t)->name,(*t)->len);
    if (delta<0)
        remove=aa_remove(&(*t)->lnk[LEFT],name,len,save(tokeep,*t),todelete);
    else
        remove=aa_remove(&(*t)->lnk[RIGHT],name,len,save(tokeep,*t),save(todelete,*t));

    // perform deletetion
    if (t==tokeep && !LTI_invalid(*todelete) && !strnncmp(name,len,(*todelete)->name,(*todelete)->len)) {
        remove=(*todelete);
        if (todelete!=tokeep) {
            (*tokeep)->lnk[LEFT] =(*todelete)->lnk[LEFT];
            (*tokeep)->lnk[RIGHT]=(*todelete)->lnk[RIGHT];
            (*todelete)=(*tokeep);
        }
        (*tokeep)=&aa_sentinel;
        LTI_release(remove);
    }

 cleanup:
    // on the way back, re rebalance
    if (((*t)->lnk[LEFT]->level < ((*t)->level-1)) || ((*t)->lnk[RIGHT]->level < ((*t)->level-1))) {
        (*t)->level--;
        if ((*t)->lnk[RIGHT]->level > (*t)->level)
            (*t)->lnk[RIGHT]->level = (*t)->level;
        aa_skew(t);
        aa_skew(&(*t)->lnk[RIGHT]);
        aa_skew(&(*t)->lnk[RIGHT]->lnk[RIGHT]);
        aa_split(t);
        aa_split(&(*t)->lnk[RIGHT]);
    }

 done:
    return remove;
}

static void *aa_metamap(LTI **lti,LTI_METAOP op,int dir) {
    int status=0;
    void *rval=NULL;
    if (LTI_invalid(*lti))
        goto done;
    int order=dir&1;
    switch(dir&TREEDIR) {
        case PREFIX:  if ((rval=op(lti)) || (rval=aa_metamap(&(*lti)->lnk[ !order],op,dir)) || (rval=aa_metamap(&(*lti)->lnk[order],op,dir))); break;
        case INFIX:   if ((rval=aa_metamap(&(*lti)->lnk[ !order],op,dir)) || (rval=op(lti)) || (rval=aa_metamap(&(*lti)->lnk[order],op,dir))); break;
        case POSTFIX: if ((rval=aa_metamap(&(*lti)->lnk[ !order],op,dir)) || (rval=aa_metamap(&(*lti)->lnk[order],op,dir)) || (rval=op(lti))); break;
    }
 done:
    return rval;
}

static void *aa_map(LTI *lti,LTI_OP op,int dir)
{
    void *metaop(LTI **lti) { op(*lti); }
    return aa_metamap(&lti,metaop,dir);
}

LTI *LTV_find(LTV *ltv,char *name,int len,int insert)
{
    int status;
    LTI *lti=NULL;
    STRY(!ltv || !name || (ltv->flags&LT_LIST),"validating LTV_find parameters");
    if (len==-1)
        len=strlen(name);
    insert=insert?INSERT:0; // true/false -> INSERT/0
    lti=aa_find(&ltv->sub.ltis,name,len,&insert);
 done:
    return LTI_invalid(lti)?NULL:lti;
}

LTI *LTV_remove(LTV *ltv,char *name,int len)
{
    LTI *todelete=NULL,*tokeep=NULL;
    return ltv->flags&LT_LIST?NULL:aa_remove(&ltv->sub.ltis,name,len,&todelete,&tokeep);
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
        ltv_count++;
        ZERO(*ltv);
        if (flags&LT_LIST)
            CLL_init(&ltv->sub.ltvs);
        else
            ltv->sub.ltis=&aa_sentinel;
        LTV_renew(ltv,data,len,flags);
    }
    TALLOC(ltv,sizeof(LTV),"LTV");
    return ltv;
}

void LTV_free(LTV *ltv)
{
    TDEALLOC(ltv,"LTV");
    if (ltv) {
        LTV_renew(ltv,NULL,0,0);
        RELEASE(ltv);
        ltv_count--;
    }
}

void *LTV_map(LTV *ltv,int dir,LTI_OP lti_op,CLL_OP cll_op)
{
    void *result=NULL;
    if (ltv) {
        if (ltv->flags&LT_LIST && cll_op)
            result=CLL_map(&ltv->sub.ltvs,dir,cll_op);
        else if (lti_op)
            result=aa_map(ltv->sub.ltis,lti_op,dir|INFIX);
    }
    return result;
}


// get a new LTVR
LTVR *LTVR_init(LTVR *ltvr,LTV *ltv)
{
    if (ltvr && ltv) {
        ltvr_count++;
        ZERO(*ltvr);
        CLL_init(&ltvr->lnk);
        ltvr->ltv=ltv;
        ltv->refs++;
    }
    TALLOC(ltvr,sizeof(LTVR),"LTVR");
    return ltvr;
}

LTV *LTVR_free(LTVR *ltvr)
{
    LTV *ltv=NULL;
    TDEALLOC(ltvr,"LTVR");
    if (ltvr) {
        if (!CLL_EMPTY(&ltvr->lnk)) { CLL_cut(&ltvr->lnk); }
        if ((ltv=ltvr->ltv))
            ltv->refs--;
        RELEASE(ltvr);
        ltvr_count--;
    }
    return ltv;
}


// get a new LTI and prepare for insertion
LTI *LTI_init(LTI *lti,char *name,int len)
{
    if (lti==NULL)
        lti=NEW(LTI);
    if (lti && name) {
        lti_count++;
        ZERO(*lti);
        lti->lnk[LEFT]=lti->lnk[RIGHT]=&aa_sentinel;
        lti->level=1;
        lti->len=len;
        lti->name=bufdup(name,len);
        for (int i=0;i<PREVIEWLEN;i++)
            lti->preview[i]=len>i?name[i]:0;
        strncpy(lti->preview,name,PREVIEWLEN);
        CLL_init(&lti->ltvs);
    }
    TALLOC(lti,sizeof(LTI),"LTI");
    return lti;
}

void LTI_free(LTI *lti)
{
    TDEALLOC(lti,"LTI");
    if (lti) {
        RELEASE(lti->name);
        RELEASE(lti);
        lti_count--;
    }
}


//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    void *op(LTI *lti) { LTI_release(lti); return NULL; }
    if (ltv) TALLOC(ltv,ltv->refs,"LTV_release (ptr/refs)");
    if (ltv && !(ltv->refs) && !(ltv->flags&LT_RO)) {
        if (ltv->flags&LT_REFS)      REF_delete(ltv); // cleans out REFS
        else if (ltv->flags&LT_LIST) CLL_release(&ltv->sub.ltvs,LTVR_release);
        else                         aa_map(ltv->sub.ltis,op,POSTFIX);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *lnk) { LTV_release(LTVR_free((LTVR *) lnk)); }

void LTI_release(LTI *lti) {
    if (lti) {
        CLL_release(&lti->ltvs,LTVR_release);
        LTI_free(lti);
    }
}


//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

// add to preop to avoid repeat visits in listree traverse
void *listree_acyclic(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
    if ((*flags&LT_TRAVERSE_LTV) && (*ltv)->flags&(LT_AVIS|LT_RVIS))
        *flags|=LT_TRAVERSE_HALT;
    return (*flags)&LT_TRAVERSE_HALT?NON_NULL:NULL;
}

void *listree_traverse(CLL *ltvs,LTOBJ_OP preop,LTOBJ_OP postop)
{
    int depth=0,cleanup=0;
    void *rval=NULL;

    void *descend_ltv(LTI *parent_lti,LTVR *parent_ltvr,LTV *ltv) {
        void *descend_lti(LTI *lti) {
            void *descend_ltvr(CLL *lnk) { // for normal-form (ltv->lti->ltvr) form ltv
                LTVR *ltvr=(LTVR *) lnk;
                return descend_ltv(lti,ltvr,ltvr->ltv);
            }

            if (!lti) goto done;
            LTV *parent=ltv;
            LT_TRAVERSE_FLAGS flags=LT_TRAVERSE_LTI;
            if (cleanup) CLL_map(&lti->ltvs,FWD,descend_ltvr);
            else if (preop && (rval=preop(&lti,NULL,&parent,depth,&flags)) ||
                     ((flags&LT_TRAVERSE_HALT) || (rval=CLL_map(&lti->ltvs,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,descend_ltvr))) ||
                     postop && ((flags|=LT_TRAVERSE_POST),(rval=postop(&lti,NULL,&parent,depth,&flags))))
                goto done;
        done:
            return rval;
        }

        void *descend_ltvr(CLL *lnk) { // for list-form ltv
            LTVR *ltvr=(LTVR *) lnk;
            return descend_ltv(NULL,ltvr,ltvr->ltv); // LTI is null in this case
        }

        if (!ltv) goto done;
        LTI *child=parent_lti;
        LT_TRAVERSE_FLAGS flags=LT_TRAVERSE_LTV;
        if (cleanup) // only descends (and cleans up) LTVs w/absolute visited flag
            return (ltv->flags&LT_AVIS && !((ltv->flags&=~LT_AVIS)&LT_AVIS) && !(ltv->flags&LT_REFS))? LTV_map(ltv,FWD,descend_lti,descend_ltvr):NULL;
        else {
            if (preop && (rval=preop(&child,parent_ltvr,&ltv,depth,&flags))) goto done;
            if ((flags&LT_TRAVERSE_HALT) || (ltv->flags&LT_REFS)) goto done;
            ltv->flags|=LT_RVIS;
            depth++;
            rval=(child!=parent_lti)?descend_lti(child):LTV_map(ltv,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,descend_lti,descend_ltvr);
            depth--;
            ltv->flags&=~LT_RVIS;
            if (rval) goto done;

            flags|=LT_TRAVERSE_POST;
            if (postop && ((rval=postop(&parent_lti,parent_ltvr,&ltv,depth,&flags)))) goto done;
        }

    done:
        if (ltv) ltv->flags|=LT_AVIS;
        return rval;
    }

    TSTART(0,"listree_traverse");
    void *traverse(CLL *lnk) { LTVR *ltvr=(LTVR *) lnk; return descend_ltv(NULL,ltvr,ltvr->ltv); }
    rval=CLL_map(ltvs,FWD,traverse);
    cleanup=1;
    preop=postop=NULL;
    CLL_map(ltvs,FWD,traverse); // clean up "visited" flags
    TFINISH(rval!=0,"listree_traverse");
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

extern LTI *LTI_first(LTV *ltv) { return (!ltv || (ltv->flags&LT_LIST))?NULL:aa_most(ltv->sub.ltis,LEFT);  }
extern LTI *LTI_last(LTV *ltv)  { return (!ltv || (ltv->flags&LT_LIST))?NULL:aa_most(ltv->sub.ltis,RIGHT); }
extern LTI *LTI_iter(LTV *ltv,LTI *lti,int dir) { dir|=ITER; return ltv&&!LTI_invalid(lti)? aa_find(&ltv->sub.ltis,lti->name,lti->len,&dir):NULL; }

LTI *LTI_lookup(LTV *ltv,LTV *name,int insert)
{
    LTI *lti=NULL;
    TLOOKUP(ltv,name->data,name->len,insert);
    if (LTV_wildcard(name))
        for (lti=LTI_first(ltv); !LTI_invalid(lti) && fnmatch_len(name->data,name->len,lti->name,-1); lti=LTI_iter(ltv,lti,FWD));
    else
        lti=LTV_find(ltv,name->data,name->len,insert);
    done:
    return lti;
}

LTI *LTI_find(LTV *ltv,char *name,int insert,int flags)
{
    LTV *nameltv=LTV_init(NEW(LTV),name,-1,flags);
    LTI *lti=LTI_lookup(ltv,nameltv,insert);
    LTV_free(nameltv);
    return lti;
}

LTI *LTI_resolve(LTV *ltv,char *name,int insert) { return LTV_find(ltv,name,-1,insert); }


int LTV_empty(LTV *ltv)
{
    if (!ltv) return true;
    else if (ltv->flags&LT_LIST) return CLL_EMPTY(&ltv->sub.ltvs);
    else return LTI_invalid(ltv->sub.ltis);
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
void LTV_erase(LTV *ltv,LTI *lti) { if (ltv && lti) LTV_remove(ltv,lti->name,lti->len); }

LTV *LTV_dup(LTV *ltv)
{
    if (!ltv) return NULL;

    int flags=ltv->flags & ~LT_NDUP;
    if (!(flags&LT_NAP))
        flags |= LT_DUP;
    return LTV_init(NEW(LTV),ltv->data,ltv->len,flags);
}

LTV *LTV_copy(LTV *ltv,unsigned maxdepth)
{
    LTV *index=LTV_NULL,*dupes=LTV_NULL;
    char buf[32];
    int index_ltv(LTV *ltv) {
        LTV *dup=NULL;
        sprintf(buf,"%p",ltv);
        if (!LT_get(index,buf,HEAD,KEEP)) {
            LT_put(index,buf,HEAD,ltv);
            if (!(ltv->flags&(LT_CVAR|LT_REFS)))
                dup=LTV_dup(ltv);
            if (dup)
                LT_put(dupes,buf,HEAD,LTV_dup(ltv));
        }
    }

    void *index_ltvs(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        if (!((*flags)&LT_TRAVERSE_HALT) && ((*flags)&LT_TRAVERSE_LTV) && depth<=maxdepth) {
            index_ltv(*ltv);
            if ((*ltv)->flags&(LT_CVAR|LT_REFS))
                (*flags)|=LT_TRAVERSE_HALT;
        }
        if (depth==maxdepth)
            (*flags)|=LT_TRAVERSE_HALT;
        return NULL;
    }
    ltv_traverse(ltv,index_ltvs,NULL)!=NULL,LTV_NULL;

    LTV *new_or_used(LTV *ltv) {
        char buf[32];
        sprintf(buf,"%p",ltv);
        LTV *rval=NULL;
        return ((rval=LT_get(dupes,buf,HEAD,KEEP)))?rval:ltv;
    }

    void *descend_lti(LTI *lti) {
        LTV *orig=LTV_peek(&lti->ltvs,HEAD);
        LTV *dupe=LT_get(dupes,lti->name,HEAD,KEEP);

        void *copy_children(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
            if (((*flags)&LT_TRAVERSE_LTV) && depth==1 && (*lti)) {
                LT_put(dupe,(*lti)->name,TAIL,new_or_used(*ltv));
                (*flags)|=LT_TRAVERSE_HALT;
            }
            return NULL;
        }

        ltv_traverse(orig,copy_children,NULL)!=NULL,LTV_NULL;
    done:
        return NULL;
    }
    LTV_map(index,FWD,descend_lti,NULL);

    LTV *result=new_or_used(ltv);
    LTV_release(index);
    LTV_release(dupes);
 done:
    return result;
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
    void *preop(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        switch ((*flags)&LT_TRAVERSE_TYPE) {
            case LT_TRAVERSE_LTI:
                if (maxdepth && depth>=maxdepth || LTI_hide(*lti))
                    (*flags)|=LT_TRAVERSE_HALT;
                else
                    fprintf(ofile,"%*c\"%s\"\n",MAX(0,depth*4-2),' ',(*lti)->name);
                break;
            case LT_TRAVERSE_LTV:
                if (pre) fprintf(ofile,"%*c%s",depth*4,' ',pre);
                else fprintf(ofile,"%*c[",depth*4,' ');
                if      ((*ltv)->flags&LT_REFS) { REF_printall(ofile,(*ltv),"REFS:\n"); fstrnprint(ofile,(*ltv)->data,(*ltv)->len); }
                else if ((*ltv)->flags&LT_BC)   disassemble(ofile,(*ltv));
                else if ((*ltv)->flags&LT_CVAR) cif_print_cvar(ofile,(*ltv),depth);
                else if ((*ltv)->flags&LT_IMM)  fprintf(ofile,"IMM 0x%x",(*ltv)->data);
                else if ((*ltv)->flags&LT_NULL) fprintf(ofile,"<null>");
                else if ((*ltv)->flags&LT_BIN)  hexdump(ofile,(*ltv)->data,(*ltv)->len);
                else                            fstrnprint(ofile,(*ltv)->data,(*ltv)->len);
                if (post) fprintf(ofile,"%s",post);
                else fprintf(ofile,"]\n");

                if ((*flags)&LT_TRAVERSE_HALT) // already visited
                    fprintf(ofile,"%*c (subtree omitted...)\n",MAX(0,depth*4-2),' ');

                if (LTV_hide(*ltv))
                    (*flags)|=LT_TRAVERSE_HALT;
                break;
            default:
                break;
        }
    done:
        return NULL;
    }

    if (ltvs)
        listree_traverse(ltvs,preop,NULL);
    else
        fprintf(ofile,"NULL");

    fflush(ofile);
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
        fprintf(ofile,"\"LTI%x\" [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        fprintf(ofile,"\"LTV%x\" -> \"LTI%x\" [color=blue]\n",ltv,lti);
        fprintf(ofile,"\"LTI%x\" -> \"%x\"\n",lti,&lti->ltvs);
        cll2dot(&lti->ltvs,NULL);
    }

    void lti_ltvr2dot(LTI *lti,LTVR *ltvr) { ltvr2dot(&ltvr->lnk); }
    void ltv_ltvr2dot(LTV *ltv,LTVR *ltvr) { ltvr2dot(&ltvr->lnk); }

    void ltv_descend(LTV *ltv) {
        if (!LTV_empty(ltv)) {
            if (ltv->flags&LT_LIST) {
                if (ltv->flags&LT_REFS)
                    REF_dot(ofile,ltv,"REFS");
                fprintf(ofile,"\"LTV%x\" -> \"%x\" [color=blue]\n",ltv,&ltv->sub.ltvs);
                //  cll2dot(&ltv->sub.ltvs,NULL);
            } else {
                fprintf(ofile,"subgraph cluster_%d { subgraph { /*rank=same*/\n",i++);
                for (LTI *lti=LTI_first(ltv);!LTI_invalid(lti);lti=LTI_iter(ltv,lti,FWD))
                    fprintf(ofile,"\"LTI%x\"\n",lti);
                fprintf(ofile,"}}\n");
                fprintf(ofile,"\"LTV%x\" -> \"LTI%x\" [color=blue]\n",ltv,ltv->sub.ltis);
            }
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

    void *preop(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        if ((*flags)&LT_TRAVERSE_LTI) {
            if (maxdepth && depth>=maxdepth || LTI_hide(*lti))
                *flags|=LT_TRAVERSE_HALT;
            else
                lti2dot(*ltv,*lti);
        } else if ((*flags)&LT_TRAVERSE_LTV) {
            if (*lti)
                lti_ltvr2dot(*lti,ltvr);
            else if (ltvr)
                ltv_ltvr2dot(*ltv,ltvr);
            if (!((*flags)&LT_TRAVERSE_HALT)) {
                ltv2dot(ltvr,*ltv,depth,flags);
                if (LTV_hide(*ltv))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    ltv_descend(*ltv);
            }
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
        if (!LTV_empty(ltv) && !(ltv->flags&LT_LIST)) {
            fprintf(ofile,"subgraph cluster_%d { subgraph { /*rank=same*/\n",i++);
            for (LTI *lti=LTI_first(ltv);!LTI_invalid(lti);lti=LTI_iter(ltv,lti,FWD))
                fprintf(ofile,"\"LTI%x\"\n",lti);
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

    void *preop(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        if ((*flags)&LT_TRAVERSE_LTI) {
            if (maxdepth && depth>=maxdepth || LTI_hide(*lti))
                *flags|=LT_TRAVERSE_HALT;
            else
                lti2dot(*ltv,*lti);
        } else if ((*flags)&LT_TRAVERSE_LTV) {
            if (*lti)
                lti_ltvr2dot(*lti,ltvr);
            else if (ltvr)
                ltv_ltvr2dot(*ltv,ltvr);
            if (!((*flags)&LT_TRAVERSE_HALT)) {
                ltv2dot(ltvr,*ltv,depth,flags);
                if (LTV_hide(*ltv))
                    *flags|=LT_TRAVERSE_HALT;
                else
                    ltv_descend(*ltv);
            }
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

CLL *LTV_list(LTV *ltv) { return (ltv && ltv->flags&LT_LIST)? &ltv->sub.ltvs:NULL; }

LTV *LTV_enq(CLL *ltvs,LTV *ltv,int end) { return LTV_put((ltvs),(ltv),(end),NULL); }
LTV *LTV_deq(CLL *ltvs,int end)          { return LTV_get((ltvs),POP,(end),NULL,NULL); }
LTV *LTV_peek(CLL *ltvs,int end)         { return LTV_get((ltvs),KEEP,(end),NULL,NULL); }

LTV *LT_put(LTV *parent,char *name,int end,LTV *child) {
    if (parent && name && child) {
        LTI *lti=LTI_resolve(parent,name,true);
        return lti?LTV_enq(&lti->ltvs,child,end):NULL;
    }
    else
        return NULL;
}

LTV *LT_get(LTV *parent,char *name,int end,int pop) {
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

REF *REF_HEAD(LTV *ltv) { return ((REF *) CLL_HEAD(&(ltv)->sub.ltvs)); }
REF *REF_TAIL(LTV *ltv) { return ((REF *) CLL_TAIL(&(ltv)->sub.ltvs)); }

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
            if (ltvr->ltv->flags==LT_NULL && LTV_empty(ltvr->ltv)) //////// no flags at all?????
                LTVR_release(lnk);
        }
        CLL_map(&ref->lti->ltvs,FWD,prune_placeholders);
        if (CLL_EMPTY(&ref->lti->ltvs)) // if LTI is pruneable
            LTV_erase(root,ref->lti); // prune it
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
        STRY(!LTV_empty(refs),"promoting non-empty ltv to ref");
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

int REF_resolve(LTV *root_ltv,LTV *refs,int insert)
{
    int status=0;
    LTV *root=root_ltv;
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
                if ((status=LTI_invalid(ref->lti=LTI_lookup(root,name,insert))))
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
                for (ref->lti=LTI_iter(root,ref->lti,FWD); !LTI_invalid(ref->lti) && fnmatch_len(name->data,name->len,ref->lti->name,-1); ref->lti=LTI_iter(root,ref->lti,FWD)); // find next lti
                if (CLL_EMPTY(&lti->ltvs)) // if LTI is pruneable
                    LTV_erase(root,lti); // prune it
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
    print_ltvs(ofile,"root(",&ref->root,") ",1);
    print_ltvs(ofile,"key(",&ref->keys,") ",1);
    fprintf(ofile,"lti(%x) ",REF_lti(ref));
    print_ltv(ofile,"ltv(",REF_ltv(ref),")",1);
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
