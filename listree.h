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

#include <stdio.h>
#include "cll.h"
#include "rbtree.h"

extern int ltv_count,ltvr_count,lti_count;
#define RBR struct rb_root
#define RBN struct rb_node

typedef enum {
    LT_NONE =0x0,
    LT_DUP  =1<<0x00, // bufdup'ed on LTV_new, not ref to existing buf
    LT_OWN  =1<<0x01, // handed malloc'ed buffer, responsible for freeing
    LT_DEP  =1<<0x02, // dependent upon another ltv's data
    LT_ESC  =1<<0x03, // strip escapes (changes buf contents and len!)
    LT_BIN  =1<<0x04, // data is binary/unprintable
    LT_CVAR =1<<0x05, // LTV data is a C variable
    LT_TYPE =1<<0x06, // CVAR of type TYPE_INFO (for reflection)
    LT_NIL  =1<<0x07, // false
    LT_NULL =1<<0x08, // empty (as opposed to false)
    LT_IMM  =1<<0x09, // immediate value, not a pointer
    LT_MACRO=1<<0x0a, // 'macro'
    LT_NOWC =1<<0x0b, // do not do wildcard matching
    LT_RO   =1<<0x0c, // META: disallow release
    LT_AVIS =1<<0x0d, // META: absolute traversal visitation flag
    LT_RVIS =1<<0x0e, // META: recursive traversal visitation flag
    LT_LIST =1<<0x0f, // META: hold children in unlabeled list, rather than default rbtree
    LT_NAP  =LT_IMM|LT_NIL|LT_NULL,         // not a pointer
    LT_FREE =LT_DUP|LT_OWN,                 // need to free data upon release
    LT_NSTR =LT_NAP|LT_BIN|LT_CVAR,         // not a string
    LT_VOID =LT_NIL|LT_NULL,                // a placeholder node, internal use only!
    LT_META =LT_RO|LT_AVIS|LT_RVIS|LT_LIST, // need to be preserved during LTV_renew
} LTV_FLAGS;

typedef struct {
    CLL repo[0]; // union without union semantics
    union {
        CLL ltvs;
        RBR ltis;
    } sub;
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
} LTV; // LisTree Value

typedef struct {
    CLL repo[0]; // union without union semantics
    CLL lnk;
    LTV *ltv;
    LTV_FLAGS flags;
} LTVR; // LisTree Value Reference

typedef struct {
    CLL repo[0]; // union without union semantics
    RBN rbn;
    CLL ltvs;
    char *name;
} LTI; // LisTree Item

typedef void *(*RB_OP)(RBN *rbn);

extern RBR *RBR_init(RBR *rbr);
extern void RBN_release(RBR *rbr,RBN *rbn,void (*rbn_release)(RBN *rbn));
extern void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn));
extern LTI *RBR_find(RBR *rbr,char *name,int len,int insert);

extern LTV *LTV_renew(LTV *ltv,void *data,int len,LTV_FLAGS flags);
extern LTV *LTV_new(void *data,int len,LTV_FLAGS flags);
extern void LTV_free(LTV *ltv);
extern void *LTV_map(LTV *ltv,int reverse,RB_OP rb_op,CLL_OP cll_op);

extern LTVR *LTVR_new(LTV *ltv);
extern LTV *LTVR_free(LTVR *ltvr);

extern LTI *LTI_new(char *name,int len);
extern void LTI_free(LTI *lti);

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////
extern void LTV_release(LTV *ltv);
extern void LTVR_release(CLL *cll);
extern void LTI_release(RBN *rbn);

//////////////////////////////////////////////////
// Combined pre-, in-, and post-fix LT traversal
//////////////////////////////////////////////////
typedef enum {
    LT_TRAVERSE_LTI=1<<0,      // LTOBJ_OP is for an LTI
    LT_TRAVERSE_LTV=1<<1,      // LTOBJ_OP is for an LTV
    LT_TRAVERSE_LTI_LTVR=1<<2, // LTOBJ_OP is for an LTVR from an LTI (normal case)
    LT_TRAVERSE_LTV_LTVR=1<<3, // LTOBJ_OP is for an LTVR from a list-form LTV
    LT_TRAVERSE_LTVR=LT_TRAVERSE_LTI_LTVR|LT_TRAVERSE_LTV_LTVR,
    LT_TRAVERSE_TYPE=LT_TRAVERSE_LTI|LT_TRAVERSE_LTV|LT_TRAVERSE_LTVR,
    LT_TRAVERSE_HALT=1<<4,     // LTOBJ_OP can set to signal no further traversal from this node
    LT_TRAVERSE_REVERSE=1<<5,  // LTOBJ_OP can set to traverse the "next level" in reverse order
} LT_TRAVERSE_FLAGS;
typedef void *(*LTOBJ_OP)(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags);
void *listree_traverse(CLL *ltvs,LTOBJ_OP preop,LTOBJ_OP postop);
void *ltv_traverse(LTV *ltv,LTOBJ_OP preop,LTOBJ_OP postop);
extern void *listree_acyclic(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags);

//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

#define LTV_NIL  LTV_new(NULL,0,LT_NIL)
#define LTV_NULL LTV_new(NULL,0,LT_NULL)
#define LTV_VOID LTV_new(NULL,0,LT_VOID)

extern LTI *LTI_first(LTV *ltv);
extern LTI *LTI_last(LTV *ltv);
extern LTI *LTI_next(LTI *lti);
extern LTI *LTI_prev(LTI *lti);
extern LTI *LTI_lookup(LTV *ltv,LTV *name,int insert); // find (or insert) lti matching "name" in ltv
extern LTI *LTI_resolve(LTV *ltv,char *name,int insert);

extern int LTV_empty(LTV *ltv);
extern LTV *LTV_put(CLL *ltvs,LTV *ltv,int end,LTVR **ltvr);
extern LTV *LTV_get(CLL *ltvs,int pop,int dir,LTV *match,LTVR **ltvr); //

extern LTV *LTV_dup(LTV *ltv);

extern LTV *LTV_enq(CLL *ltvs,LTV *ltv,int end);
extern LTV *LTV_deq(CLL *ltvs,int end);
extern LTV *LTV_peek(CLL *ltvs,int end);

extern int LTV_wildcard(LTV *ltv);

extern void print_ltv(FILE *ofile,char *pre,LTV *ltv,char *post,int maxdepth);
extern void print_ltvs(FILE *ofile,char *pre,CLL *ltvs,char *post,int maxdepth);

extern void ltvs2dot(FILE *ofile,CLL *ltvs,int maxdepth,char *label);
extern void ltvs2dot_simple(FILE *ofile,CLL *ltvs,int maxdepth,char *label);

extern void graph_ltvs(FILE *ofile,CLL *ltvs,int maxdepth,char *label);
extern void graph_ltvs_to_file(char *filename,CLL *ltvs,int maxdepth,char *label);


//////////////////////////////////////////////////
// Basic LT construction
//////////////////////////////////////////////////

extern LTV *LT_put(LTV *parent,char *name,int end,LTV *child);
extern LTV *LT_get(LTV *parent,char *name,int end,int pop);

//////////////////////////////////////////////////
// REF (Listree's text-based API)
//////////////////////////////////////////////////
typedef struct REF {
    CLL lnk;
    CLL keys;   // name(/value) lookup key(s)
    CLL root;   // LTV being queried
    LTI *lti;   // name lookup result
    LTVR *ltvr; // value lookup result
    int reverse;
} REF;

#define REF_HEAD(cll) ((REF *) CLL_HEAD(cll))
#define REF_TAIL(cll) ((REF *) CLL_TAIL(cll))

extern int REF_create(char *data,int len,CLL *refs);
extern int REF_delete(CLL *refs); // clears refs, prunes listree branch

extern LTV *REF_reset(REF *ref,LTV *newroot);

extern int REF_resolve(LTV *root,CLL *refs,int insert);
extern int REF_iterate(CLL *refs,int remove);

extern int REF_assign(REF *ref,LTV *ltv);
extern int REF_remove(REF *ref);

extern LTI *REF_lti(REF *ref);
extern LTVR *REF_ltvr(REF *ref);
extern LTV *REF_ltv(REF *ref);
extern LTV *REF_key(REF *ref);

extern void REF_print(FILE *ofile,REF *ref,char *label);
extern void REF_printall(FILE *ofile,CLL *refs,char *label);
extern void REF_dot(FILE *ofile,CLL *refs,char *label);

#endif
