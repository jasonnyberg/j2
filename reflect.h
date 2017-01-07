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

#include "jli.h"

typedef enum
{
    TYPE_NONE,
    TYPE_INT1S,
    TYPE_INT2S,
    TYPE_INT4S,
    TYPE_INT8S,
    TYPE_INT1U,
    TYPE_INT2U,
    TYPE_INT4U,
    TYPE_INT8U,
    TYPE_FLOAT4,
    TYPE_FLOAT8,
    TYPE_FLOAT12
} TYPE_UTYPE;

typedef union // keep members aligned with associated dutype enum
{
    TYPE_UTYPE dutype;
    struct { TYPE_UTYPE dutype; unsigned char        val; } int1u;
    struct { TYPE_UTYPE dutype; unsigned short       val; } int2u;
    struct { TYPE_UTYPE dutype; unsigned long        val; } int4u;
    struct { TYPE_UTYPE dutype; unsigned long long   val; } int8u;
    struct { TYPE_UTYPE dutype; signed   char        val; } int1s;
    struct { TYPE_UTYPE dutype; signed   short       val; } int2s;
    struct { TYPE_UTYPE dutype; signed   long        val; } int4s;
    struct { TYPE_UTYPE dutype; signed   long long   val; } int8s;
    struct { TYPE_UTYPE dutype;          float       val; } float4;
    struct { TYPE_UTYPE dutype;          double      val; } float8;
    struct { TYPE_UTYPE dutype;          long double val; } float12;
} TYPE_UVALUE;


typedef struct TYPE_INFO
{
    char *type_id;                     // id
    char *category;                    // kind of item (base, struct, etc.
    char *DW_AT_name;                  // symbol
    char *DW_AT_type;                  // referenced type
    char *DW_AT_const_value;           // enum val
    char *DW_AT_byte_size;             // 
    char *DW_AT_bit_size;              // 
    char *DW_AT_bit_offset;            // 
    char *DW_AT_data_member_location;  //
    char *DW_AT_location;              //
    char *DW_AT_encoding;              //
    char *DW_AT_upper_bound;           //
    unsigned index;                    //
    unsigned level;                    //
    DICT_ITEM *children;               //
    DICT_ITEM *item;                   //
    
    char *typename;                    // human readable cross-referenced type name
    DICT_ITEM *nexttype;               // string containing pointer to referenced type dict entry (cheat)

    char *addr;                        // set by Type_combine...
    char *name;                        // set by Type_combine...
} TYPE_INFO;

extern long long *Type_getLocation(char *loc);

extern char *reflect_enumstr(char *type,unsigned int val);
extern void reflect_vardump(char *type,void *addr,char *prefix);
extern void reflect_varmember(char *type,void *addr,char *member);
extern void reflect_pushvar(char *type,void *addr);

extern void reflect_init(char *binname,char *fifoname);
extern void reflect(char *command);

typedef int (*Type_traverseFn)(DICT *dict,void *data,TYPE_INFO *type_info);

#endif
