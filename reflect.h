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

#ifndef REFLECT_H
#define REFLECT_H

#include "dwarf.h"
#include "libdwarf.h"

typedef struct
{
    Dwarf_Off id;
    Dwarf_Off base;

    Dwarf_Half tag; // kind of item (base, struct, etc.
    char *name;
    char *const_value; // enum val
    Dwarf_Unsigned bytesize;
    Dwarf_Unsigned bitsize;
    Dwarf_Unsigned bitoffset;
    void *data_member_location;
    void *location;
    char *encoding;
    unsigned upper_bound;
} TYPE_INFO;

extern long long *Type_getLocation(char *loc);

extern char *reflect_enumstr(char *type,unsigned int val);
extern void reflect_vardump(char *type,void *addr,char *prefix);
extern void reflect_varmember(char *type,void *addr,char *member);
extern void reflect_pushvar(char *type,void *addr);

extern void reflect_init(char *binname,char *fifoname);
extern void reflect(char *command);

#endif
