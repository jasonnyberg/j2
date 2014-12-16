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
    LT_NIL= 1<<0, // false
    LT_DUP= 1<<1, // bufdup data for new LTV
    LT_ESC= 1<<2, // strip escapes (changes buf and len!)
    LT_RO=  1<<3, // never release LTV/children
    LT_CVAR=1<<4, // LTV data is a C variable
    LT_AVIS=1<<5, // absolute traversal visitation flag
    LT_RVIS=1<<6, // recursive traversal visitation flag
} LTV_FLAGS;

typedef struct
{
    CLL repo[0]; // union without union semantics
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
    RBR rbr;
} LTV; // LisTree Value

typedef struct
{
    CLL repo[0]; // union without union semantics
    CLL cll;
    LTV *ltv;
} LTVR; // LisTree Value Reference

typedef struct
{
    CLL repo[0]; // union without union semantics
    RBN rbn;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*RB_OP)(RBN *rbn);

extern RBR *RBR_init(RBR *rbr);
extern void RBN_release(RBR *rbr,RBN *rbn,void (*rbn_release)(RBN *rbn));
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn));
extern LTI *RBR_find(RBR *rbr,char *name,int len,int insert);

extern LTV *LTV_new(void *data,int len,LTV_FLAGS flags);
extern void LTV_free(LTV *ltv);
extern void LTV_commit(LTV *ltv);
extern void *LTV_map(LTV *ltv,int reverse,RB_OP op);

extern LTVR *LTVR_new(LTV *ltv);
extern void LTVR_free(LTVR *ltvr);

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

extern LTV *LTV_put(CLL *ltvr_cll,LTV *ltv,int end,LTVR **ltvr);
extern LTV *LTV_get(CLL *ltvr_cll,int pop,int end,void *match,int matchlen,LTVR **ltvr);

extern LTV *LTV_push(CLL *ltvr_cll,LTV *ltv);
extern LTV *LTV_pop(CLL *ltvr_cll);

extern void print_ltv(LTV *ltv,int maxdepth);
extern void print_ltvs(CLL *cll,int maxdepth);

extern void LT_init();

#endif

