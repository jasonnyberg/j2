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

CLL ltv_repo,ltvr_repo,lti_repo;
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


// get a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,LTV_FLAGS flags)
{
    LTV *ltv=NULL;
    if (!data)
    {
        data="";
        flags|=LT_DUP;
        len=0;
    }
    if ((ltv=(LTV *) CLL_get(&ltv_repo,POP,TAIL)) || (ltv=NEW(LTV)))
    {
        ltv_count++;
        ltv->len=len<0?strlen((char *) data):len;
        ltv->flags=flags;
        ltv->data=data;
        if (flags&LT_DUP) (ltv->flags|=LT_DEL),(ltv->data=bufdup(ltv->data,ltv->len));
        if (flags&LT_ESC) strstrip(ltv->data,&ltv->len);
    }
    return ltv;
}

void LTV_free(LTV *ltv)
{
    ZERO(*ltv);
    CLL_put(&ltv_repo,&ltv->repo[0],HEAD);
    ltv_count--;
}


// get a new LTVR
LTVR *LTVR_new(LTV *ltv)
{
    LTVR *ltvr=(LTVR *) CLL_get(&ltvr_repo,POP,TAIL);
    if (ltvr || (ltvr=NEW(LTVR)))
    {
        ltvr->ltv=ltv;
        ltvr_count++;
    }
    return ltvr;
}

void LTVR_free(LTVR *ltvr)
{
    ZERO(*ltvr);
    CLL_put(&ltvr_repo,&ltvr->repo[0],HEAD);
    ltvr_count--;
}


// get a new LTI and prepare for insertion
LTI *LTI_new(char *name,int len)
{
    LTI *lti;
    if (name && ((lti=(LTI *) CLL_get(&lti_repo,POP,TAIL)) || (lti=NEW(LTI))))
    {
        lti_count++;
        lti->name=bufdup(name,len);
        CLL_init(&lti->cll);
    }
    return lti;
}

void LTI_free(LTI *lti)
{
    ZERO(*lti);
    CLL_put(&lti_repo,&lti->repo[0],HEAD);
    lti_count--;
}


// return node that owns "name", inserting if desired AND required.
LTI *LT_find(RBR *rbr,char *name,int len,int insert)
{
    LTI *lti=NULL;
    
    if (rbr && name)
    {
        RBN *parent=NULL,**rbn = &(rbr->rb_node);
        while (*rbn)
        {
            int result = strnncmp(name,len,((LTI *) *rbn)->name,-1);
            if (!result) return (LTI *) *rbn; // found it!
            else (parent=*rbn),(rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right);
        }
        if (insert && (lti=LTI_new(name,len)))
        {
            rb_link_node(&lti->rbn,parent,rbn); // add
            rb_insert_color(&lti->rbn,rbr); // rebalance
        }
    }
    return lti;
}

void *RBR_traverse(RBR *rbr,int reverse,RB_OP op,void *data)
{
    RBN *rbn=NULL,*next,*result=NULL;
    if (reverse)
        for (rbn=rb_last(rbr); rbn && (next=rb_prev(rbn),!(result=op(rbn,data)));rbn=next);
    else
        for (rbn=rb_first(rbr);rbn && (next=rb_next(rbn),!(result=op(rbn,data)));rbn=next);
    return result;
}

//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

void *LTV_traverse(LTV *ltv,void *data)
{
    void *rval=NULL;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltv) goto done;
    
    if (!ltobj_data) // remove absoloute visited flag
        return (ltv->flags&LT_AVIS && !((ltv->flags&=~LT_AVIS)&LT_AVIS) && ltv->rbr.rb_node)?
            RBR_traverse(&ltv->rbr,0,LTI_traverse,NULL):NULL;
    else if (!(ltv->flags&LT_RVIS))
    {
        if (ltobj_data->preop && (rval=ltobj_data->preop(NULL,NULL,ltv,data))) goto done;
        
        ltv->flags|=LT_RVIS;
        ltobj_data->depth++;
        if (!ltobj_data->halt && ltv->rbr.rb_node) rval=RBR_traverse(&ltv->rbr,0,LTI_traverse,data);
        ltobj_data->depth--;
        ltv->flags&=~LT_RVIS;
        if (rval) goto done;
        
        if (ltobj_data->postop && (rval=ltobj_data->postop(NULL,NULL,ltv,data))) goto done;
    }
    
 done:
    if (ltv) ltv->flags|=LT_AVIS;
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

void *LTVR_traverse(CLL *cll,void *data)
{
    void *rval=NULL;
    LTVR *ltvr = (LTVR *) cll;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!ltvr) goto done;
    
    if (ltobj_data && ltobj_data->preop && (rval=ltobj_data->preop(NULL,ltvr,NULL,data))) goto done;
    if ((!ltobj_data || !ltobj_data->halt) && ltvr->ltv && (rval=LTV_traverse(ltvr->ltv,data))) goto done;
    if (ltobj_data && ltobj_data->postop && (rval=ltobj_data->postop(NULL,ltvr,NULL,data))) goto done;
    
 done:
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}

void *LTI_traverse(RBN *rbn,void *data)
{
    void *rval=NULL;
    LTI *lti=(LTI *) rbn;
    struct LTOBJ_DATA *ltobj_data = (struct LTOBJ_DATA *) data;
    if (!lti) goto done;
    
    if (ltobj_data && ltobj_data->preop && (rval=ltobj_data->preop(lti,NULL,NULL,data))) goto done;
    if ((!ltobj_data || !ltobj_data->halt) && (rval=CLL_map(&lti->cll,FWD,LTVR_traverse,data))) goto done;
    if (ltobj_data && ltobj_data->postop && (rval=ltobj_data->postop(lti,NULL,NULL,data))) goto done;
    
 done:
    if (ltobj_data) ltobj_data->halt=0;
    return rval;
}


//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

void *listree_traverse(CLL *ltvr_cll,LTOBJ_OP preop,LTOBJ_OP postop,void *data)
{
    void *rval=NULL;
    struct LTOBJ_DATA ltobj_data = { preop,postop,data,0,0 };
    void *ltvr_op(CLL *cll,void *data) { return LTVR_traverse(cll,data); }
    rval=CLL_map(ltvr_cll,FWD,ltvr_op,&ltobj_data);
    CLL_map(ltvr_cll,FWD,ltvr_op,NULL); // cleanup "visited" flags
    return rval;
}

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && !ltv->refs && !(ltv->flags&LT_RO))
    {
        RBR_release(&ltv->rbr,LTI_release);
        if (ltv->flags&LT_DEL) DELETE(ltv->data);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *cll)
{
    LTVR *ltvr=(LTVR *) cll;
    if (ltvr)
    {
        ltvr->ltv->refs--;
        LTV_release(ltvr->ltv);
        LTVR_free(ltvr);
    }
}

void LTI_release(RBN *rbn)
{
    LTI *lti=(LTI *) rbn;
    if (lti)
    {
        CLL_release(&lti->cll,LTVR_release);
        DELETE(lti->name);
        LTI_free(lti);
    }
}


//////////////////////////////////////////////////
// Basic LT insert/remove
//////////////////////////////////////////////////

LTV *LTV_put(CLL *cll,LTV *ltv,int end,LTVR **ltvr_ret)
{
    int status=0;
    LTVR *ltvr=NULL;
    if (cll && ltv && (ltvr=LTVR_new(ltv)))
    {
        if (CLL_put(cll,(CLL *) ltvr,end))
        {
            ltv->refs++;
            if (ltvr_ret) *ltvr_ret=ltvr;
            return ltv; //!!
        }
        else LTVR_free(ltvr);
    }
    return NULL;
}

LTV *LTV_get(CLL *cll,int pop,int end,void *match,int matchlen,LTVR **ltvr_ret)
{
    void *ltv_match(CLL *cll,void *data)
    {
        LTVR *ltvr=(LTVR *) cll;
        if (!ltvr || !ltvr->ltv || ltvr->ltv->len!=matchlen || memcmp(ltvr->ltv->data,match,matchlen)) return NULL;
        else return pop?CLL_pop(cll):cll;
    }

    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (match && matchlen<0) matchlen=strlen(match);
    if (!(ltvr=(LTVR *) match?CLL_map(cll,end,ltv_match,NULL):CLL_get(cll,pop,end)))
        return NULL;
    ltv=ltvr->ltv;
    ltv->refs-=pop;
    if (pop)
    {
        LTVR_free(ltvr);
        ltvr=NULL;
    }
    if (ltvr_ret) (*ltvr_ret)=ltvr;
    return ltv;
}

LTV *LTV_push(CLL *cll,LTV *ltv) { return LTV_put(cll,ltv,HEAD,NULL); }
LTV *LTV_pop(CLL *cll)           { return LTV_get(cll,1,HEAD,NULL,0,NULL); }

void LT_init()
{
    CLL_init(&ltv_repo);
    CLL_init(&ltvr_repo);
    CLL_init(&lti_repo);
}
