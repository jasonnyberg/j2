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

#include <dwarf.h>
#include <libdwarf.h>

#include "listree.h"
/*
 * [index]/[dependencies]
 *   "die_id"
 *     [type_info]
 *      "type~id"
 *        [die_id]
 *      "type~base"
 *        [die_id]
 *      "type~name"
 *        [name]
 *      "type~symb" (pass 2)
 *        [symbolic name]
 * [type]/[variable]/[function]
 *   "symbolic name"
 *     [type_info]...
 */

#define TYPE_ID   "type id"   // a die's offset
#define TYPE_BASE "type base" // a die's base's offset
#define TYPE_NAME "type name" // a die's type name
#define TYPE_SYMB "type symb" // a die's composite name
#define SYMB_BASE "symb base" // link to base's composite name

#define CVAR_KIND "cvar kind" // what kind of cvar is this (i.e. what to cast ltv->data to)
#define CVAR_TYPE "cvar type" // cvar's associated TYPE_INFO

#define DIE_FORMAT "0x%x"     // format for a die's DOT-language element id
#define CVAR_FORMAT "CVAR_%x" // format for a CVAR's DOT-language element id


typedef enum
{
    TYPE_NONE    =    0x0,
    TYPE_INT1S   = 1<<0x0,
    TYPE_INT2S   = 1<<0x1,
    TYPE_INT4S   = 1<<0x2,
    TYPE_INT8S   = 1<<0x3,
    TYPE_INT1U   = 1<<0x4,
    TYPE_INT2U   = 1<<0x5,
    TYPE_INT4U   = 1<<0x6,
    TYPE_INT8U   = 1<<0x7,
    TYPE_FLOAT4  = 1<<0x8,
    TYPE_FLOAT8  = 1<<0x9,
    TYPE_FLOAT12 = 1<<0xa,
    TYPE_ADDR    = 1<<0xb,
    TYPE_INTS    = TYPE_INT1S | TYPE_INT2S | TYPE_INT4S | TYPE_INT8S,
    TYPE_INTU    = TYPE_INT1U | TYPE_INT2U | TYPE_INT4U | TYPE_INT8U,
    TYPE_FLOAT   = TYPE_FLOAT4 | TYPE_FLOAT8 | TYPE_FLOAT12
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
    struct { TYPE_UTYPE dutype;               void * val; } addr;
} TYPE_UVALUE;

typedef struct {
    Dwarf_Off      offset;
    Dwarf_Unsigned next_cu_header_offset;
    Dwarf_Unsigned header_length;
    Dwarf_Half     version_stamp;
    Dwarf_Unsigned abbrev_offset;
    Dwarf_Half     address_size;
    Dwarf_Half     length_size;
    Dwarf_Half     extension_size;
} CU_DATA;

typedef enum {
    TYPEF_DQ         = 1<<0x0,
    TYPEF_BASE       = 1<<0x1,
    TYPEF_CONSTVAL   = 1<<0x2,
    TYPEF_BYTESIZE   = 1<<0x3,
    TYPEF_BITSIZE    = 1<<0x4,
    TYPEF_BITOFFSET  = 1<<0x5,
    TYPEF_ENCODING   = 1<<0x6,
    TYPEF_UPPERBOUND = 1<<0x7,
    TYPEF_LOWPC      = 1<<0x8,
    TYPEF_MEMBERLOC  = 1<<0x9,
    TYPEF_LOCATION   = 1<<0xa,
    TYPEF_ADDR       = 1<<0xb,
    TYPEF_EXTERNAL   = 1<<0xc
} TYPE_FLAGS;

typedef struct
{
    Dwarf_Off id; // global offset
    Dwarf_Off base; // global offset
    Dwarf_Off parent;
    struct {
    TYPE_FLAGS flags;
    Dwarf_Half tag; // kind of item (base, struct, etc.
    Dwarf_Signed const_value; // enum val
    Dwarf_Unsigned bytesize;
    Dwarf_Unsigned bitsize;
    Dwarf_Unsigned bitoffset;
    Dwarf_Unsigned encoding;
    Dwarf_Unsigned upper_bound;
    Dwarf_Unsigned low_pc;
    Dwarf_Addr data_member_location;
    Dwarf_Signed location; // ??
    Dwarf_Bool external;
    Dwarf_Unsigned addr; // from loclist
    } attr;
} TYPE_INFO;



extern int print_cvar(FILE *ofile,LTV *ltv);
extern int dot_cvar(FILE *ofile,LTV *ltv);
extern int curate_module(LTV *mod_ltv);
extern int preview_module(LTV *mod_ltv);

#if 0
extern long long *Type_getLocation(char *loc);

extern char *reflect_enumstr(char *type,unsigned int val);
extern void reflect_vardump(char *type,void *addr,char *prefix);
extern void reflect_varmember(char *type,void *addr,char *member);
extern void reflect_pushvar(char *type,void *addr);

extern void reflect_init(char *binname,char *fifoname);
extern void reflect(char *command);
#endif

#endif
