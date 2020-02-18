/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 * Copyright (C) 2018 Jason Nyberg <jasonnyberg@gmail.com> (dual-licensed)
 * (C) Copyright 2019 Hewlett Packard Enterprise Development LP.
 *
 * This file is part of j2.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either
 *
 *   * the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version
 *
 * or
 *
 *   * the GNU General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version
 *
 * or both in parallel, as here.
 *
 * j2 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and
 * the GNU Lesser General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LISTREE_H
#define LISTREE_H

#include <functional>

//////////////////////////////////////////////////
// LisTree (Valtree w/collision lists)
//////////////////////////////////////////////////

#include "options.h"

#include <stdio.h>
#include "cll.h"

extern int show_ref;

typedef enum {
    LT_NONE =0,
    LT_DUP  =0x00000001, // bufdup'ed on LTV_new, not ref to existing buf
    LT_OWN  =0x00000002, // handed malloc'ed buffer, responsible for freeing
    LT_ESC  =0x00000004, // strip escapes (changes buf contents and len!)
    LT_BIN  =0x00000008, // data is binary/unprintable
    LT_REFS =0x00000010, // LTV holds a list of REFs (implies LT_LIST)
    LT_CVAR =0x00000020, // LTV data is a C variable
    LT_TYPE =0x00000040, // CVAR of type TYPE_INFO (for reflection)
    LT_FFI  =0x00000080, // CVAR of type ffi_type (for reflection)
    LT_CIF  =0x00000100, // CVAR of type ffi_cif (for reflection)
    LT_NULL =0x00000200, // empty
    LT_IMM  =0x00000400, // immediate value, not a pointer
    LT_ARR  =0x00000800, // data is a pointer to 1st element of an array
    LT_NOWC =0x00001000, // do not do wildcard matching
    LT_BC   =0x00002000, // VM bytecode
    LT_DERV =0x00004000, // Derived from another LTV (cannot be an LT_LIST)

    LT_RO   =0x00010000, // META: disallow release
    LT_AVIS =0x00020000, // META: absolute traversal visitation flag
    LT_RVIS =0x00040000, // META: recursive traversal visitation flag
    LT_LIST =0x00080000, // META: hold children in unlabeled list, rather than default rbtree
    LT_NAP  =LT_IMM|LT_NULL,                // not a pointer
    LT_FREE =LT_DUP|LT_OWN,                 // need to free data upon release
    LT_META =LT_RO|LT_AVIS|LT_RVIS|LT_LIST, // need to be preserved during LTV_renew
    LT_REFL =LT_TYPE|LT_FFI|LT_CIF,         // used for reflection; visibility controlled by "show_ref"
    LT_NSTR =LT_NAP|LT_BIN|LT_CVAR|LT_REFL, // not a string
    LT_NDUP =LT_FREE|LT_REFS|LT_CVAR|LT_REFL|LT_LIST, // need to be excised during LTV_dup
} LTV_FLAGS;

struct LTI;
typedef struct LTI LTI;

typedef struct {
    union {
        CLL ltvs;
        LTI *ltis;
    } sub;
    LTV_FLAGS flags;
    void *data;
    int len;
    int refs;
#ifdef VIZ
    fvec pos,vel;
#endif
} LTV; // LisTree Value

typedef struct {
    CLL lnk;
    LTV *ltv;
} LTVR; // LisTree Value Reference

enum { RIGHT=0,LEFT=1,PREVIEWLEN=2,INSERT=4,ITER=8 }; // RIGHT==FWD, LEFT==REV

// FWD/REV define whether Left is processed before right or vice/versa for each of INFIX/PREFIX/POSTFIX
enum { /*FWD=0,REV=1,*/ INFIX=1<<1,PREFIX=2<<1,POSTFIX=3<<1,TREEDIR=INFIX|PREFIX|POSTFIX };

struct LTI {
    LTI *lnk[2]; // AA TREE LEFT/RIGHT
    char *name;
    CLL ltvs;
    char level,len,preview[PREVIEWLEN];
#ifdef VIZ
    fvec pos,vel;
#endif
};

typedef std::function<void *(LTI *)> LTI_OP;

extern LTV *LTV_init(LTV *ltv,void *data,int len,LTV_FLAGS flags);
extern LTV *LTV_renew(LTV *ltv,void *data,int len,LTV_FLAGS flags);
extern void LTV_free(LTV *ltv);
extern int  LTV_is_empty(LTV *ltv);
extern void *LTV_map(LTV *ltv,int reverse,LTI_OP lti_op,CLL_OP cll_op);
extern LTI *LTV_find(LTV *ltv,char *name,int len,int insert);
extern LTI *LTV_remove(LTV *ltv,char *name,int len);

extern LTVR *LTVR_init(LTVR *ltvr,LTV *ltv);
extern LTV *LTVR_free(LTVR *ltvr);

extern LTI *LTI_init(LTI *lti,char *name,int len);
extern void LTI_free(LTI *lti);
extern int LTI_invalid(LTI *lti);

//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////
extern void LTV_release(LTV *ltv);
extern void LTVR_release(CLL *cll);
extern void LTI_release(LTI *lti);

//////////////////////////////////////////////////
// Combined pre-, in-, and post-fix LT traversal
//////////////////////////////////////////////////
typedef enum {
    LT_TRAVERSE_LTI     =1<<0, // LTOBJ_OP is for an LTI
    LT_TRAVERSE_LTV     =1<<1, // LTOBJ_OP is for an LTV from an LTI (normal case)
    LT_TRAVERSE_POST    =1<<2, // LTOBJ_OP in postfix pass
    LT_TRAVERSE_HALT    =1<<3, // LTOBJ_OP can set to signal no further traversal from this node
    LT_TRAVERSE_REVERSE =1<<4, // LTOBJ_OP can set to traverse the "next level" in reverse order
    LT_TRAVERSE_TYPE    =LT_TRAVERSE_LTI|LT_TRAVERSE_LTV,
} LT_TRAVERSE_FLAGS;
typedef std::function<void *(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags)> LTOBJ_OP;
void *listree_traverse(CLL *ltvs,LTOBJ_OP preop,LTOBJ_OP postop);
void *ltv_traverse(LTV *ltv,LTOBJ_OP preop,LTOBJ_OP postop);
extern void *listree_acyclic(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags);

//////////////////////////////////////////////////
// Dictionary
//////////////////////////////////////////////////

#define LTV_NULL      LTV_init(NEW(LTV),NULL,0,LT_NULL)
#define LTV_ZERO      LTV_init(NEW(LTV),NULL,sizeof(NULL),LT_IMM)
#define LTV_NULL_LIST LTV_init(NEW(LTV),NULL,0,LT_NULL|LT_LIST)

extern LTI *LTI_first(LTV *ltv);
extern LTI *LTI_last(LTV *ltv);
extern LTI *LTI_iter(LTV *ltv,LTI *lti,int dir);
extern LTI *LTI_lookup(LTV *ltv,LTV *name,int insert); // find (or insert) lti matching "name" in ltv
extern LTI *LTI_find(LTV *ltv,char *name,int insert,int flags); // wraps name with LTV/flags
extern LTI *LTI_resolve(LTV *ltv,char *name,int insert); // lookup, via string name no wildcards

extern int LTV_empty(LTV *ltv);
extern LTV *LTV_put(CLL *ltvs,LTV *ltv,int end,LTVR **ltvr);
extern LTV *LTV_get(CLL *ltvs,int pop,int dir,LTV *match,LTVR **ltvr); //
extern void LTV_erase(LTV *ltv,LTI *lti);

extern LTV *LTV_dup(LTV *ltv);
extern LTV *LTV_copy(LTV *ltv,int maxdepth);
extern LTV *LTV_concat(LTV *a,LTV *b);
extern int LTV_wildcard(LTV *ltv);

extern void print_ltv(FILE *ofile,char *pre,LTV *ltv,char *post,int maxdepth);
extern void print_ltvs(FILE *ofile,char *pre,CLL *ltvs,char *post,int maxdepth);

extern void ltvs2dot(FILE *ofile,CLL *ltvs,int maxdepth,char *label);
extern void ltvs2dot_simple(FILE *ofile,CLL *ltvs,int maxdepth,char *label);

extern void graph_ltvs(FILE *ofile,CLL *ltvs,int maxdepth,char *label);
extern void graph_ltvs_to_file(char *filename,CLL *ltvs,int maxdepth,char *label);
extern void graph_ltv_to_file(char *filename,LTV *ltv,int maxdepth,char *label);

extern CLL *LTV_list(LTV *ltv);

extern LTV *LTV_enq(CLL *ltvs,LTV *ltv,int end);
extern LTV *LTV_deq(CLL *ltvs,int end);
extern LTV *LTV_peek(CLL *ltvs,int end);

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
    LTV *cvar;  // cvar deref result
    int reverse;
} REF;

extern REF *REF_HEAD(LTV *ltv);
extern REF *REF_TAIL(LTV *ltv);

extern LTV *REF_create(LTV *refs);
extern int REF_delete(LTV *refs); // clears refs, prunes listree branch

extern LTV *REF_reset(REF *ref,LTV *newroot);

extern int REF_resolve(LTV *root_ltv,LTV *refs,int insert);
extern int REF_iterate(LTV *refs,int pop);

extern int REF_assign(LTV *refs,LTV *ltv);
extern int REF_replace(LTV *refs,LTV *ltv);
extern int REF_remove(LTV *refs);

extern LTI *REF_lti(REF *ref);
extern LTVR *REF_ltvr(REF *ref);
extern LTV *REF_ltv(REF *ref);
extern LTV *REF_key(REF *ref);

extern void REF_print(FILE *ofile,REF *ref,char *label);
extern void REF_printall(FILE *ofile,LTV *refs,char *label);
extern void REF_dot(FILE *ofile,LTV *refs,char *label);

#endif
