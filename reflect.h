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

#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>
#include <ffi.h>

#include "listree.h"

/*
 * [index]/[dependencies]
 *   "die_id"
 *     [type_info]
 *      "type~id"
 *        [die_id]
 *      "type~name"
 *        [name]
 *      "type~base"
 *        [base cvar]
 * [type]/[variable]/[function]
 *   "symbolic name"
 *     [type_info]...
 */

#define TYPE_NAME "die name" // a die's type name
#define TYPE_SYMB "die symb" // a die's symbolic type name
#define TYPE_BASE "die base" // a die's base's ltv
#define TYPE_LIST "die list" // type's children, in die order
#define TYPE_CAST "die cast" // a casted cvar's original data (lifespan protection)
#define TYPE_META "die meta" // a die's pointer-type parent

#define FFI_TYPE  "ffi type"  // FFI data assocated with type
#define FFI_CIF   "ffi cif"   // FFI data assocated with type

#define DIE_FORMAT "\"%s\""       // format for a die's DOT-language element id
#define CVAR_FORMAT "\"CVAR_%x\"" // format for a CVAR's DOT-language element id

#define TYPE_IDLEN 9
#define DWARF_ID(str,global_offset) snprintf((str),TYPE_IDLEN,"%08x",(global_offset))
#define DWARF_ALIAS(str,sig8) sprintf((str),CODE_BLUE "%08x %08x" CODE_RESET,                 \
                                      ntohl(*(unsigned *) &(sig8).signature[0]),              \
                                      ntohl(*(unsigned *) &(sig8).signature[4]))

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
    TYPE_FLOAT16 = 1<<0xa,
    TYPE_ADDR    = 1<<0xb,
    TYPE_INTS    = TYPE_INT1S | TYPE_INT2S | TYPE_INT4S | TYPE_INT8S | TYPE_ADDR,
    TYPE_INTU    = TYPE_INT1U | TYPE_INT2U | TYPE_INT4U | TYPE_INT8U,
    TYPE_FLOAT   = TYPE_FLOAT4 | TYPE_FLOAT8 | TYPE_FLOAT16
} TYPE_UTYPE;

typedef union // keep members aligned with associated dutype enum
{
    struct { TYPE_UTYPE dutype;                           } base;
    struct { TYPE_UTYPE dutype; unsigned char        val; } int1u;
    struct { TYPE_UTYPE dutype; unsigned short       val; } int2u;
    struct { TYPE_UTYPE dutype; unsigned int         val; } int4u;
    struct { TYPE_UTYPE dutype; unsigned long long   val; } int8u;
    struct { TYPE_UTYPE dutype; signed   char        val; } int1s;
    struct { TYPE_UTYPE dutype; signed   short       val; } int2s;
    struct { TYPE_UTYPE dutype; signed   int         val; } int4s;
    struct { TYPE_UTYPE dutype; signed   long long   val; } int8s;
    struct { TYPE_UTYPE dutype;          float       val; } float4;
    struct { TYPE_UTYPE dutype;          double      val; } float8;
    struct { TYPE_UTYPE dutype;          long double val; } float16;
    struct { TYPE_UTYPE dutype;          void *      val; } addr;
} TYPE_UVALUE;

typedef struct {
    char           offset_str[TYPE_IDLEN];
    char           next_cu_header_offset_str[TYPE_IDLEN];
    Dwarf_Unsigned next_cu_header_offset;
    Dwarf_Unsigned header_length;
    Dwarf_Half     version_stamp;
    Dwarf_Unsigned abbrev_offset;
    Dwarf_Half     address_size;
    Dwarf_Half     length_size;
    Dwarf_Half     extension_size;
    Dwarf_Bool     is_info;
    Dwarf_Sig8     sig8;
    Dwarf_Unsigned offset;
} CU_DATA;

typedef enum {
    TYPEF_BASE       = 1<<0x00,
    TYPEF_CONSTVAL   = 1<<0x01,
    TYPEF_BYTESIZE   = 1<<0x02,
    TYPEF_BITSIZE    = 1<<0x03,
    TYPEF_BITOFFSET  = 1<<0x04,
    TYPEF_ENCODING   = 1<<0x05,
    TYPEF_UPPERBOUND = 1<<0x06,
    TYPEF_LOWPC      = 1<<0x07,
    TYPEF_MEMBERLOC  = 1<<0x08,
    TYPEF_LOCATION   = 1<<0x09,
    TYPEF_ADDR       = 1<<0x0a,
    TYPEF_EXTERNAL   = 1<<0x0b,
    TYPEF_VECTOR     = 1<<0x0c,
    TYPEF_SYMBOLIC   = 1<<0x0d,
    TYPEF_SIGNATURE  = 1<<0x0e, // new for dwarf v4
    TYPEF_IS_INFO    = 1<<0x0f, // new for dwarf v4
    TYPEF_OFFSET     = 1<<0x10, // new for dwarf v4
} TYPE_FLAGS;


typedef struct
{
    LTV ltv;
    int depth;
    char id_str[TYPE_IDLEN];     // global offset as a string
    char base_str[TYPE_IDLEN];   // global offset as a string
    Dwarf_Off die;
    Dwarf_Off base;
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
    Dwarf_Unsigned addr; // from loclist
    Dwarf_Bool external; // new for dwarf v4
    Dwarf_Unsigned offset; // new for dwarf v4
    Dwarf_Sig8 sig8; // used in dwarf v4
} TYPE_INFO_LTV;

extern LTV *cif_module;

extern LTV *cif_create_cvar(LTV *type,void *data,char *member);
extern LTV *cif_assign_cvar(LTV *cvar,LTV *ltv);
extern int cif_print_cvar(FILE *ofile,LTV *ltv,int depth);
extern int cif_dot_cvar(FILE *ofile,LTV *ltv);

extern int cif_curate_module(LTV *mod_ltv,int bootstrap);
extern int cif_preview_module(LTV *mod_ltv);
extern int cif_ffi_prep(LTV *type);

extern LTV *cif_rval_create(LTV *lambda,void *data);
extern int cif_args_marshal(LTV *lambda,int dir,int (*marshal)(char *argname,LTV *type));
extern LTV *cif_get_meta(LTV *ltv);
extern LTV *cif_put_meta(LTV *ltv,LTV *meta);
extern LTV *cif_coerce_i2c(LTV *arg,LTV *type);
extern LTV *cif_coerce_c2i(LTV *arg);
extern int cif_ffi_call(LTV *type,void *loc,LTV *rval,CLL *coerced_ltvs);

extern LTV *cif_type_info(char *typename);
extern LTV *cif_find_base(LTV *type,int tag);
extern LTV *cif_find_concrete(LTV *type);
extern LTV *cif_find_indexable(LTV *type);
extern LTV *cif_find_function(LTV *type);

extern int cif_dump_module(char *ofilename,LTV *module);
extern LTV *cif_isaddr(LTV *cvar);

extern int cif_iszero(LTV *cvar);
extern int cif_ispos(LTV *cvar);
extern int cif_isneg(LTV *cvar);

extern LTV *cif_create_closure(LTV *function_type,void (*thunk) (ffi_cif *CIF, void *RET, void**ARGS, void *USER_DATA));

#endif
