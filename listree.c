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

LTV *LTV_get(CLL *ltvs,int pop,int end,void *match,int matchlen,LTVR **ltvr_ret)
{
    void *ltv_match(CLL *lnk) {
        LTVR *ltvr=(LTVR *) lnk;
        if (!ltvr || !ltvr->ltv || ltvr->ltv->flags&LT_IMM || ltvr->ltv->len!=matchlen || memcmp(ltvr->ltv->data,match,matchlen)) return NULL;
        else return pop?CLL_cut(lnk):lnk;
    }

    LTVR *ltvr=NULL;
    LTV *ltv=NULL;
    if (match && matchlen<0) matchlen=strlen(match);
    if (!(ltvr=(LTVR *) match?CLL_map(ltvs,end,ltv_match):CLL_get(ltvs,pop,end)))
        return NULL;
    ltv=ltvr->ltv;
    if (pop) {
        LTVR_free(ltvr);
        ltvr=NULL;
    }
    if (ltvr_ret) (*ltvr_ret)=ltvr;
    return ltv;
}

LTV *LTV_enq(CLL *ltvs,LTV *ltv,int end) { return LTV_put(ltvs,ltv,end,NULL); }
LTV *LTV_deq(CLL *ltvs,int end)          { return LTV_get(ltvs,POP,end,NULL,0,NULL); }
LTV *LTV_peek(CLL *ltvs,int end)         { return LTV_get(ltvs,KEEP,end,NULL,0,NULL); }

void print_ltv(FILE *ofile,char *pre,LTV *ltv,char *post,int maxdepth)
{
    char *indent="                                                                                                                ";
    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        if (*lti) {
            if (maxdepth && depth>=maxdepth) *flags|=LT_TRAVERSE_HALT;
            fstrnprint(stdout,indent,depth*4);
            fprintf(ofile,"\"%s\"\n",(*lti)->name);
        }

        if (*ltv) {
            fstrnprint(ofile,indent,depth*4+2);
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

    listree_traverse(ltv,preop,NULL);
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
// resolver
//////////////////////////////////////////////////

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
                    LTV_enq(&acc_tok->ref->lti_parent,(*ltv),HEAD); // record to be able to free lti if empty
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




//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

CLL ref_repo;
int ref_count=0;

REF *REF_new(LTI *lti);
void REF_free(REF *ref);

REF *refpush(CLL *cll,REF *ref) { return (REF *) CLL_put(cll,&ref->lnk,HEAD); }
REF *refpop(CLL *cll)           { return (REF *) CLL_get(cll,POP,HEAD); }
REF *reftail(CLL *cll)          { return (REF *) CLL_get(cll,KEEP,TAIL); }

REF *REF_new()
{
    static CLL *repo=NULL;
    if (!repo) repo=CLL_init(&ref_repo);

    REF *ref=NULL;
    if (len && ((ref=refpop(repo)) || ((ref=NEW(REF)) && CLL_init(&ref->lnk))))
    {
        CLL_init(&ref->keys);
        CLL_init(&ref->lti_parent);
        ref->lti=NULL;
        ref->ltvr=NULL;
        CLL_init(&ref->ltvs);

        ref_count++;
    }
    return ref;
}

void REF_free(REF *ref)
{
    if (!ref) return;
    CLL_cut(&ref->lnk); // take it out of any list it's in

    CLL_release(&ref->keys,LTVR_release);
    LTV *parent=LTV_peek(&ref->lti_parent,HEAD);
    if (parent && ref->lti && CLL_EMPTY(&ref->lti.ltvs)) // if LTI empty and pruneable
        RBN_release(&parent->sub.ltis,&ref->lti.rbn,LTI_release); // prune it
    CLL_release(&ref->lti_parent,LTVR_release);
    ref->lti=NULL;
    ref->ltvr=NULL;
    CLL_release(&ref->ltvs,LTVR_release);

    refpush(&ref_repo,ref);
    ref_count--;
}

//////////////////////////////////////////////////
// API
//////////////////////////////////////////////////

int LT_resolve(char *data,int len,LTV *root,int insert,CLL *refs)
{
    int wildcard(LTV *key) { series(key->data,key->len,NULL,"*?",NULL) < key->len; }

    int parse()
    {
        int advance(int bump) { bump=MIN(bump,len); data+=bump; len-=bump; return bump; }

        int name() { return series(data,len,NULL,".[",NULL); }
        int val()  { return series(data,len,NULL,NULL,"[]"); } // val=data+1,len-2!!!
        int sep()  { return series(data,len,".",NULL,NULL);  }


        int status=0;
        unsigned flags,tlen;
        REF *ref;

        while (len) {
            ref=NULL;

            // parse name (mandatory)
            STRY(!(tlen=name()),"parsing ref name");
            STRY(!(ref=CLL_put(refs,REF_new(),HEAD)),"enqueing ref");
            STRY(!LTV_enq(&ref->keys,LTV_new(data,tlen,0),TAIL),"enqueueing name key");
            advance(tlen);

            // parse vals (optional)
            while ((tlen=val()))
                STRY(!LTV_enq(&ref->keys,LTV_new(data+1,tlen-2,0),TAIL),"enqueueing val key");

            // if there's anything left, it has to be a separator
            if (len)
                STRY(advance(sep())==1),"parsing ref sep");
        }

        done:
        return status;
    }

    LTV *name,*val;
    int reverse;
    int resolve_keys(REF *ref) {
        int status=0;
        STRY(!ref,"validating ref");
        STRY(!(name=LTV_peek(&ref->keys,HEAD)),"validating name key"); // name is first key
        reverse=series(name->data,1,NULL,"-",NULL);
        ltv_val=CLL_next(&ref->keys,&ltv->name.lnk,FWD); // val will be next key
        done:
        return status;
    }

    LTV *ref_resolve(REF *ref,LTV *root) { // create and/or fill in ref from scratch, return
        int status=0;
        STRY(!root,"validating root");
        STRY(!resolve_keys(ref),"resolving ref keys");

        done: return status;
    }

    void *getnext(CLL *lnk) {
        int status=0;
        REF *ref=(REF *) lnk;

        STRY(resolve_keys(ref),"resolving ref keys");

        LTVR *ltvr=NULL;
        if (ref->lti) {
            LTV *ref_ltv=NULL;
            LTI *lti=NULL;
            ref_ltv=LTV_peek(&ref_head->ltvs,HEAD);
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

        done:
        return status?(REF_free(ref),NULL):ref;
    }

    if (CLL_EMPTY(refs))
        STRY(parse(),"parsing reference");
    else
        TRYCATCH(!CLL_map(refs,getnext,FWD),-1,done,"performing getnext"); // if no next, refs will be empty

    if (!CLL_EMPTY(refs))
        TRYCATCH(!CLL_map(refs,get,REV),-1,done,"performing getnext"); // if no next, refs will be empty

    done:
    return status?NULL:ref_tail;
}


int LT_release(CLL *refs) {
    void REF_release(CLL *lnk) { REF_free((REF *) lnk); }
    CLL_release(refs,REF_release);
}
