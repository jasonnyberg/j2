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

//////////////////////////////////////////////////
// Circular linked-list (sentinel implementation)
//////////////////////////////////////////////////

#ifndef CLL_H
#define CLL_H

// head=lnk[0],tail=lnk[1]
struct CLL { struct CLL *lnk[2]; } __attribute__((aligned(sizeof(long))));
typedef struct CLL CLL;

enum { HEAD=0,TAIL=1,FWD=0,REV=1,KEEP=0,POP=1 };

extern CLL *CLL_init(CLL *lst);                         // init and return lst
extern void CLL_release(CLL *lst,void (*op)(CLL *cll)); // pop each list item and call op on it

extern CLL *CLL_splice(CLL *a,CLL *b,int end);  // convert a<->a' and b<->b' to a<->b and a'<->b'
extern CLL *CLL_cut(CLL *lnk);                  // cut lnk from list it's in and return it/NULL
extern CLL *CLL_get(CLL *lst,int pop,int end);  // get/pop lst's head or tail, return it/NULL
extern CLL *CLL_put(CLL *lst,CLL *lnk,int end); // add lnk to lst's head or tail, return it

// calls op(lnk,data) for each lnk in lst until op returns non-zero; returns what last op returns
typedef void *(*CLL_OP)(CLL *lnk);
extern void *CLL_map(CLL *sentinel,int dir,CLL_OP op);

#define CLL_SIB(x,end) ((x)->lnk[end])
#define CLL_EMPTY(sentinel) (!CLL_get((sentinel),FWD,KEEP))
#define CLL_ROT(sentinel,dir) (CLL_put((sentinel),CLL_get((sentinel),POP,(dir)),!(dir))
#define CLL_MERGE(dst,src,dir) (CLL_splice((dst),(src),(dir)),CLL_cut(src))

#endif
