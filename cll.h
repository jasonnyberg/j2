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
// Circular linked-list (nee StaQ) (sentinel implementation)
//////////////////////////////////////////////////

#ifndef CLL_H
#define CLL_H

// head=lnk[0],tail=lnk[1]
struct CLL { struct CLL *lnk[2]; } __attribute__((aligned(sizeof(long))));
typedef struct CLL CLL;

enum { HEAD=0,TAIL=1,FWD=0,REV=1,KEEP=0,POP=1 };

extern CLL *CLL_init(CLL *lst);                         // init and return lst
extern void CLL_release(CLL *lst,void (*op)(CLL *cll)); // pop each list item and call op on it

extern CLL *CLL_sumi(CLL *a,CLL *b,int end); //
extern CLL *CLL_pop(CLL *lnk);                  // pop lnk from list it's in and return it/NULL
extern CLL *CLL_get(CLL *lst,int dir,int pop);  // get/pop lst's head or tail, return it/NULL


// call op(lnk,data) for each lnk in lst until op returns non-zero; return what last op returns
extern void *CLL_traverse(CLL *lst,int dir,void *(*op)(CLL *lnk,void *data),void *data);

#define CLL_EMPTY(sentinel) (!CLL_get((sentinel),FWD,KEEP))
#define CLL_ROT(sentinel,dir) (CLL_put((sentinel),CLL_get((sentinel),POP,(dir)),!(dir)))

#endif
C