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

#include <stdlib.h>
#include "cll.h"

//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

#define LINK(x,y,end) (CLL_SIB((y),!(end))=(x),CLL_SIB((x),(end))=(y))

CLL *CLL_init(CLL *cll) { return cll?CLL_SIB(cll,FWD)=CLL_SIB(cll,REV)=cll:NULL; }
void CLL_release(CLL *sentinel,void (*op)(CLL *cll)) { CLL *cll; for (cll=NULL;(cll=CLL_get(sentinel,POP,HEAD));op(cll)); }

// convert a<->a' and b'<->b to a<->b and a'<->b', i.e. "splice OUT sub-CLL a thru b" OR "splice CLL b INTO CLL a at dir=HEAD/TAIL"
CLL *CLL_splice(CLL *a,CLL *b,int end) { return a && b? (LINK(CLL_SIB(a,end),CLL_SIB(b,!end),!end),LINK(a,b,end)):NULL; }
//CLL *CLL_cut(CLL *lnk) { return lnk?CLL_init(CLL_splice(CLL_SIB(lnk,FWD),CLL_SIB(lnk,REV),REV)):NULL; }
CLL *CLL_cut(CLL *lnk) { return lnk?CLL_splice(lnk,lnk,FWD):NULL; }
CLL *CLL_get(CLL *sentinel,int pop,int end) { CLL *lnk=NULL; return sentinel && (lnk=CLL_SIB(sentinel,end))!=sentinel? (pop?CLL_cut(lnk):lnk):NULL; }
CLL *CLL_put(CLL *sentinel,CLL *lnk,int end) { return CLL_splice(sentinel,CLL_init(lnk),end); }
CLL *CLL_next(CLL *sentinel,CLL *lnk,int dir) { CLL *rlnk=lnk?CLL_SIB(lnk,dir):CLL_get(sentinel,KEEP,dir); return rlnk==sentinel?NULL:rlnk; }
void *CLL_mapfrom(CLL *sentinel,CLL *ff,int dir,CLL_OP op) { // map with fast-forward
    void *rval=NULL;
    CLL *sib=NULL,*next=NULL;
    for(rval=NULL,(sib=CLL_SIB(ff?ff:sentinel,dir)); // init
        sib && sib!=sentinel && (next=CLL_SIB(sib,dir)) && !(rval=op(sib)); // next saved, op can cut
        sib=next);
    return rval;
}
void *CLL_map(CLL *sentinel,int dir,CLL_OP op) { return CLL_mapfrom(sentinel,NULL,dir,op); }
int CLL_len(CLL *sentinel) { int acc=0; return sentinel?(CLL_map(sentinel,FWD,[&](CLL *lnk) { acc++; return (CLL *) NULL; }),acc):0; }
