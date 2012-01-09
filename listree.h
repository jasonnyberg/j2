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
    LT_DUP=1<<0, // bufdup data for new LTV
        LT_ESC=1<<1, // strip escapes (changes buf and len!)
        LT_DEL=1<<2,  // free not-referenced LTV data upon release
        LT_RO=1<<3,   // never release LTV/children
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
    void *metadata;
} LTVR; // LisTree Value Reference

typedef struct
{
    CLL repo[0]; // union without union semantics
    RBN rbn;
    char *name;
    CLL cll;
} LTI; // LisTreeItem

typedef void *(*RB_OP)(RBN *rbn,void *data);

extern RBR *RBR_init(RBR *rbr);
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn));
extern void *RBR_traverse(RBR *rbr,int reverse,RB_OP op,void *data);

extern LTV *LTV_new(void *data,int len,LTV_FLAGS flags);
extern void LTV_free(LTV *ltv);

extern LTVR *LTVR_new(void *metadata);
extern void *LTVR_free(LTVR *ltvr);

extern LTI *LTI_new(char *name,int len);
extern void LTI_free(LTI *lti);

extern LTI *LT_find(RBR *rbr,char *name,int len,int insert);

//////////////////////////////////////////////////
// Tag Team of traverse methods for LT elements
//////////////////////////////////////////////////

typedef void *(*LTOBJ_OP)(LTVR *ltvr,LTI *lti,LTV *ltv,void *data);
struct LTOBJ_DATA { LTOBJ_OP preop; LTOBJ_OP postop; unsigned depth; void *data; int halt:1; };

void *LTV_traverse(LTV *ltv,void *data);
void *LTVR_traverse(CLL *cll,void *data);
void *LTI_traverse(RBN *rbn,void *data);

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////
extern void LTV_release(LTV *ltv);
extern void LTVR_release(CLL *cll);
extern void LTI_release(RBN *rbn);

//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

extern LTV *LTV_put(CLL *cll,LTV *ltv,int end,void *metadata);
extern LTV *LTV_get(CLL *cll,int pop,int end,void *match,int matchlen,LTVR **ltvr);

extern void LT_init();

#endif

