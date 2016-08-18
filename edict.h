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


#include "listree.h"

typedef struct EDICT
{
    CLL dict;
    CLL contexts;
} EDICT;


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

//  vm
extern int edict_init(EDICT *edict);
extern int edict_eval(EDICT *edict);
extern int edict_destroy(EDICT *edict);

/*
extern LTV *edict_add(RBR *rbr,LTV *ltv);

extern LTV *edict_push(RBR *rbr,char *buf,int len);
extern LTV *edict_strpush(RBR *rbr,char *str);

extern void *edict_pop(RBR *rbr,char *name,int len);
extern void *edict_strpop(RBR *rbr,char *name);

extern void edict_nameitem(LTV *ltv,char *name,int len);
extern void edict_name(RBR *rbr,char *name,int len);
extern void edict_rename(RBR *rbr,char *old,int oldlen,char *new,int newlen);

extern LTV *edict_clone(LTV *ltv,int sibs);
extern int edict_copy_item(RBR *rbr,LTV *ltv);
extern int edict_copy(RBR *rbr,char *name,int len);
extern int edict_raise(RBR *rbr,char *name,int len);

extern void *edict_lookup(RBR *rbr,char *name,int len);

extern void edict_display_item(LTV *ltv,char *prefix);
extern void edict_list(RBR *rbr,char *buf,int len,int count,char *prefix);
extern void edict_strlist(RBR *rbr,char *str,int count,char *prefix);

extern int edict_len(RBR *rbr,char *buf,int len);
extern int edict_strlen(RBR *rbr,char *str);

extern LTV *edict_getitem(RBR *rbr,char *name,int len,int pop);
extern LTV *edict_getitems(RBR *rbr,LTV *repos,int display);

extern LTV *edict_get_nth_item(RBR *rbr,int n);

*/
