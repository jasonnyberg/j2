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

#include <stdlib.h>
#include "cll.h"

//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

#define SIB(x,end) ((x)->lnk[end])
#define LINK(x,y,end) (SIB((y),!(end))=(x),SIB((x),(end))=(y))

CLL *CLL_init(CLL *cll) { return SIB(cll,FWD)=SIB(cll,REV)=cll; }
void CLL_release(CLL *sentinel,void (*op)(CLL *cll)) { for (CLL *cll=NULL;cll=CLL_get(sentinel,POP,HEAD);op(cll)); }

// sumi-gaeshi - 4 corners throw
// convert a<->a' and b<->b' to a'<->b' and a<->b
CLL *CLL_sumi(CLL *a,CLL *b,int end) { return a && b? (LINK(SIB(a,end),SIB(b,!end),!end),LINK(a,b,end)):NULL; }
CLL *CLL_pop(CLL *lnk) { return lnk?CLL_init(CLL_sumi(SIB(lnk,FWD),SIB(lnk,REV),REV)):NULL; }
CLL *CLL_get(CLL *sentinel,int end,pop) {
    CLL *cll;
    return sentinel && (cll=SIB(sentinel,end))!=sentinel? (pop?CLL_pop(cll):cll):NULL;
}
void *CLL_traverse(CLL *sentinel,int dir,CLL_OP op,void *data)
{
    for(CLL *rval=NULL,sib=SIB(sentinel,dir),next;
        sib && sib!=sentinel && (next=SIB(sib,dir)) && !(rval=op(sib,data));
        sib=next);
    return rval;
}
