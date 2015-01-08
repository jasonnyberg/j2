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


#ifndef LISTREE_H
#define LISTREE_H

//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

extern int ltv_count,ltvr_count,lti_count;

#include "cll.h"
#include "rbtree.h"

#define RBR struct rb_root
#define RBN struct rb_node

typedef enum {
    LT_DUP =1<<0x00, // bufdup data for new LTV, free upon release
    LT_OWN =1<<0x01, // responsible for freeing data
    LT_DEP =1<<0x02, // dependent upon another ltv's data
    LT_ESC =1<<0x03, // strip escapes (changes buf and len!)
    LT_RO  =1<<0x04, // disallow release
    LT_BIN =1<<0x05, // data is binary/unprintable
    LT_CVAR=1<<0x06, // LTV data is a C variable
    LT_AVIS=1<<0x07, // absolute traversal visitation flag
    LT_RVIS=1<<0x08, // recursive traversal visitation flag
    LT_LIST=1<<0x09, // hold children in unlabeled list, rather than default rbtree
    LT_ROOT=1<<0x0a, // root of a dict rather than a namespace
    LT_GC  =1<<0x0b, // garbage collect this node before deleting
    LT_NIL =1<<0x0c, // false
    LT_NULL=1<<0x0d, // empty (as opposed to false)
    LT_IMM =1<<0x0e|LT_NIL|LT_NULL, // immediate value, not a pointer
    LT_FREE=LT_DUP|LT_OWN,  // need to free data upon release
    LT_NSTR=LT_IMM|LT_BIN, // not a string
} LTV_FLAGS;

typedef struct
{
    CLL repo[0]; // union without union semantics
    union {
        CLL ltvrs;
        RBR ltis;
    } sub;
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
} LTV; // LisTree Value

typedef struct
{
    CLL repo[0]; // union without union semantics
    CLL lnk;
    LTV *ltv;
} LTVR; // LisTree Value Reference

typedef struct
{
    CLL repo[0]; // union without union semantics
    RBN rbn;
    CLL ltvrs;
    char *name;
} LTI; // LisTree Item

typedef void *(*RB_OP)(RBN *rbn);

extern RBR *RBR_init(RBR *rbr);
extern void RBN_release(RBR *rbr,RBN *rbn,void (*rbn_release)(RBN *rbn));
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn));
extern LTI *RBR_find(RBR *rbr,char *name,int len,int insert);

extern LTV *LTV_new(void *data,int len,LTV_FLAGS flags);
extern void LTV_free(LTV *ltv);
extern void LTV_commit(LTV *ltv);
extern void *LTV_map(LTV *ltv,int reverse,RB_OP rb_op,CLL_OP cll_op);

extern LTVR *LTVR_new(LTV *ltv);
extern LTV *LTVR_free(LTVR *ltvr);

extern LTI *LTI_new(char *name,int len);
extern void LTI_free(LTI *lti);

//////////////////////////////////////////////////
// Combined pre-, in-, and post-fix LT traversal
//////////////////////////////////////////////////

typedef void *(*LTOBJ_OP)(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *halt);
void *listree_traverse(LTV *ltv,LTOBJ_OP preop,LTOBJ_OP postop);

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////
extern void LTV_release(LTV *ltv);
extern void LTVR_release(CLL *cll);
extern void LTI_release(RBN *rbn);

//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

extern LTV *LTV_put(CLL *ltvrs,LTV *ltv,int end,LTVR **ltvr);
extern LTV *LTV_get(CLL *ltvrs,int pop,int end,void *match,int matchlen,LTVR **ltvr);

extern LTV *LTV_enq(CLL *ltvrs,LTV *ltv,int end);
extern LTV *LTV_deq(CLL *ltvrs,int end);

extern void print_ltv(LTV *ltv,int maxdepth);
extern void print_ltvs(CLL *ltvrs,int maxdepth);

extern void LT_init();

#endif

