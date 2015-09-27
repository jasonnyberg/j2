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
    rb_erase(rbn,rbr);
    if (rbn_release)
        rbn_release(rbn);
}

void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn))
{
    RBN *rbn;
    while (rbn=rbr->rb_node)
        RBN_release(rbr,rbn,rbn_release);
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
        if (insert && (lti=LTI_new(name,len))) {
            rb_link_node(&lti->rbn,parent,rbn); // add
            rb_insert_color(&lti->rbn,rbr); // rebalance
        }
    }
    return lti;
}


// get a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,LTV_FLAGS flags)
{
    LTV *ltv=NULL;
    if ((ltv=(LTV *) CLL_get(&ltv_repo,POP,TAIL)) || (ltv=NEW(LTV))) {
        ZERO(*ltv);
        ltv_count++;
        ltv->len=(len<0 && !(flags&LT_NSTR))?strlen((char *) data):len;
        ltv->data=data;
        if (flags&LT_DUP) ltv->data=bufdup(ltv->data,ltv->len);
        if (flags&LT_ESC) strstrip(ltv->data,&ltv->len);
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
        if (ltv->flags&LT_LIST && cll_op) result=CLL_map(&ltv->sub.ltvrs,FWD,cll_op);
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
    LTVR *ltvr=(LTVR *) CLL_get(&ltvr_repo,POP,TAIL);
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
        ltvr_count--;
    }
    return ltv;
}


// get a new LTI and prepare for insertion
LTI *LTI_new(char *name,int len)
{
    LTI *lti;
    if (name && ((lti=(LTI *) CLL_get(&lti_repo,POP,TAIL)) || (lti=NEW(LTI)))) {
        ZERO(*lti);
        lti_count++;
        lti->name=bufdup(name,len);
        CLL_init(&lti->ltvrs);
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

            if (cleanup) CLL_map(&lti->ltvrs,FWD,LTVR_traverse);
            else if (preop && (rval=preop(&lti,&ltvr,&ltv,depth,&flags)) ||
                     ((flags&LT_TRAVERSE_HALT) || (rval=ltvr?LTVR_traverse(&ltvr->lnk):CLL_map(&lti->ltvrs,(flags&LT_TRAVERSE_REVERSE)?REV:FWD,LTVR_traverse))) ||
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
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && !ltv->refs) {
        if (ltv->flags&LT_LIST) CLL_release(&ltv->sub.ltvrs,LTVR_release);
        else                    RBR_release(&ltv->sub.ltis,LTI_release);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *lnk) { LTV_release(LTVR_free((LTVR *) lnk)); }

void LTI_release(RBN *rbn) {
    LTI *lti=(LTI *) rbn;
    if (lti) {
        CLL_release(&lti->ltvrs,LTVR_release);
        LTI_free(lti);
    }
}


//////////////////////////////////////////////////
// Basic LT insert/remove
//////////////////////////////////////////////////


LTV *LTV_put(CLL *ltvrs,LTV *ltv,int end,LTVR **ltvr_ret)
{
    int status=0;
    LTVR *ltvr=NULL;
    if (ltvrs && ltv && (ltvr=LTVR_new(ltv))) {
        if (CLL_put(ltvrs,&ltvr->lnk,end)) {
            if (ltvr_ret) *ltvr_ret=ltvr;
            return ltv; //!!
        }
        else LTVR_free(ltvr);
    }
    return NULL;
}

LTV *LTV_get(CLL *ltvrs,int pop,int end,void *match,int matchlen,LTVR **ltvr_ret)
{
    void *ltv_match(CLL *lnk) {
        LTVR *ltvr=(LTVR *) lnk;
        if (!ltvr || !ltvr->ltv || ltvr->ltv->flags&LT_IMM || ltvr->ltv->len!=matchlen || memcmp(ltvr->ltv->data,match,matchlen)) return NULL;
        else return pop?CLL_cut(lnk):lnk;
    }

    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (match && matchlen<0) matchlen=strlen(match);
    if (!(ltvr=(LTVR *) match?CLL_map(ltvrs,end,ltv_match):CLL_get(ltvrs,pop,end)))
        return NULL;
    ltv=ltvr->ltv;
    if (pop) {
        LTVR_free(ltvr);
        ltvr=NULL;
    }
    if (ltvr_ret) (*ltvr_ret)=ltvr;
    return ltv;
}

LTV *LTV_enq(CLL *ltvrs,LTV *ltv,int end) { return LTV_put(ltvrs,ltv,end,NULL); }
LTV *LTV_deq(CLL *ltvrs,int end)          { return LTV_get(ltvrs,POP,end,NULL,0,NULL); }

void print_ltv(LTV *ltv,int maxdepth)
{
    char *indent="                                                                                                                ";
    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        if (*lti) {
            if (maxdepth && depth>=maxdepth) *flags|=LT_TRAVERSE_HALT;
            fstrnprint(stdout,indent,depth*4);
            fprintf(stdout,"\"%s\"\n",(*lti)->name);
        }

        if (*ltv) {
            fstrnprint(stdout,indent,depth*4+2);
            fprintf(stdout,"[");
            if ((*ltv)->flags&LT_NULL)     ; // nothing
            else if ((*ltv)->flags&LT_NIL) printf("nil");
            else if ((*ltv)->flags&LT_IMM) printf("0x%p (immediate)",&(*ltv)->data);
            else if ((*ltv)->flags&LT_BIN) hexdump((*ltv)->data,(*ltv)->len);
            else                           fstrnprint(stdout,(*ltv)->data,(*ltv)->len);
            fprintf(stdout,"]\n");
        }
        return NULL;
    }

    listree_traverse(ltv,preop,NULL);
}

void print_ltvs(CLL *ltvrs,int maxdepth)
{
    void *op(CLL *lnk) { LTVR *ltvr=(LTVR *) lnk; if (ltvr) print_ltv((LTV *) ltvr->ltv,maxdepth); return NULL; }
    CLL_map(ltvrs,FWD,op);
}

void LT_init()
{
    CLL_init(&ltv_repo);
    CLL_init(&ltvr_repo);
    CLL_init(&lti_repo);
}


