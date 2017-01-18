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

#define _GNU_SOURCE // strndupa, stpcpy
#define __USE_GNU // strndupa, stpcpy
#include <sys/types.h> /* For open() */
#include <sys/stat.h>  /* For open() */
#include <fcntl.h>     /* For open() */
#include <unistd.h>    /* For close() */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <dwarf.h>
#include <libdwarf.h>

#include "util.h"
#include "listree.h"
#include "reflect.h"

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
    TYPEF_EXTERNAL   = 1<<0xc,
    TYPEF_SIBLING    = 1<<0xd
} TYPE_FLAGS;


typedef struct
{
    TYPE_FLAGS flags;
    Dwarf_Off id; // global offset
    Dwarf_Half tag; // kind of item (base, struct, etc.
    char *name;
    Dwarf_Off base; // global offset
    Dwarf_Off sibling; // global offset
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
} TYPE_INFO;


#define IF_OK(cond,followup) if (cond==DW_DLV_OK) followup


int print_type_info(TYPE_INFO *type_info)
{
    int status=0;
 done:
    return status;
}

int dot_type_info(FILE *ofile,TYPE_INFO *type_info)
{
    int status=0;
    const char *str=NULL;
    fprintf(ofile,"\"DIE_%1$x\" [shape=record label=\"{TYPE_INFO %1$x",type_info->id);
    dwarf_get_TAG_name(type_info->tag,&str);
    fprintf(ofile,"|%s %s",str+7,type_info->name?type_info->name:"");
    if (type_info->flags&TYPEF_CONSTVAL)   fprintf(ofile,"|constval %u",type_info->const_value);
    if (type_info->flags&TYPEF_BYTESIZE)   fprintf(ofile,"|bytesize %u",type_info->bytesize);
    if (type_info->flags&TYPEF_BITSIZE)    fprintf(ofile,"|bitsize %u",type_info->bitsize);
    if (type_info->flags&TYPEF_BITOFFSET)  fprintf(ofile,"|bitoffset %u",type_info->bitoffset);
    if (type_info->flags&TYPEF_ENCODING)   fprintf(ofile,"|encoding %u",type_info->encoding);
    if (type_info->flags&TYPEF_UPPERBOUND) fprintf(ofile,"|upperbound 0x%x",type_info->upper_bound);
    if (type_info->flags&TYPEF_MEMBERLOC)  fprintf(ofile,"|member location %u",type_info->data_member_location);
    if (type_info->flags&TYPEF_LOCATION)   fprintf(ofile,"|location 0x%x",type_info->location);
    if (type_info->flags&TYPEF_ADDR)       fprintf(ofile,"|addr 0x%x",type_info->addr);
    if (type_info->flags&TYPEF_EXTERNAL)   fprintf(ofile,"|external %u",type_info->external);
    fprintf(ofile,"}\"");
    if (type_info->flags&TYPEF_DQ)
        fprintf(ofile,"style=filled fillcolor=red");
    else if (!type_info->name)
        fprintf(ofile,"style=filled fillcolor=yellow");
    else
        switch (type_info->tag) {
            case DW_TAG_subprogram:       fprintf(ofile,"style=filled fillcolor=orange"); break;
            case DW_TAG_formal_parameter: fprintf(ofile,"style=filled fillcolor=gold"); break;
            case DW_TAG_variable:         fprintf(ofile,"style=filled fillcolor=cyan"); break;
            case DW_TAG_base_type:        fprintf(ofile,"style=filled fillcolor=magenta"); break;
            case DW_TAG_typedef:          fprintf(ofile,"style=filled fillcolor=green"); break;
            case DW_TAG_structure_type:   fprintf(ofile,"style=filled fillcolor=pink"); break;
            case DW_TAG_union_type:       fprintf(ofile,"style=filled fillcolor=pink"); break;
            case DW_TAG_enumeration_type: fprintf(ofile,"style=filled fillcolor=pink"); break;
            case DW_TAG_member:           fprintf(ofile,"style=filled fillcolor=lightblue"); break;
        }
    fprintf(ofile,"]\n");
    //if (type_info->flags&TYPEF_BASE)       fprintf(ofile,"\"DIE_%x\" -> \"DIE_%x\" [color=purple]\n",type_info->id,type_info->base);
    //if (type_info->flags&TYPEF_SIBLING)    fprintf(ofile,"\"DIE_%x\" -> \"DIE_%x\" [color=blue]\n",type_info->id,type_info->sibling);
 done:
    return status;
}

void graph_module_to_file(char *filename,LTV *module_ltv) {
    FILE *ofile=fopen(filename,"w");

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        if ((*ltv) && !(*lti) && (!(*ltvr) || (*ltvr)->ltv==(*ltv)))
            if ((*ltv)->flags&LT_AVIS)
                *flags|=LT_TRAVERSE_HALT;
            else if ((*ltv)->flags&LT_CVAR) {
                TYPE_INFO *type_info=(TYPE_INFO *) (*ltv)->data;
                dot_type_info(ofile,type_info);
                fprintf(ofile,"\"%x\" -> \"DIE_%x\"\n",(*ltv),type_info->id); // link ltv to type_info
            }
        return NULL;
    }


    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,module_ltv,HEAD);
    fprintf(ofile,"digraph iftree\n{\ngraph [/*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");
    ltvs2dot(ofile,&ltvs,0,filename);
    listree_traverse(&ltvs,preop,NULL);
    fprintf(ofile,"}\n");
    LTV_deq(&ltvs,HEAD);

    fclose(ofile);
}

int graph_cus_to_files(LTV *module_ltv)
{
    int status=0;
    CLL cus;
    CLL_init(&cus);
    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        int status=0;
        if ((*ltv) && !(*lti) && (!(*ltvr) || (*ltvr)->ltv==(*ltv)))
            if ((*ltv)->flags&LT_AVIS)
                *flags|=LT_TRAVERSE_HALT;
            else if ((*ltv)->flags&LT_CVAR) {
                TYPE_INFO *type_info=(TYPE_INFO *) (*ltv)->data;
                if (type_info->tag==DW_TAG_compile_unit)
                    LTV_enq(&cus,(*ltv),TAIL);
            }
    done:
        return status?NON_NULL:NULL;
    }
    
    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,module_ltv,HEAD);
    STRY(listree_traverse(&ltvs,preop,NULL)!=NULL,"traversing module in postprocess");
    LTV_deq(&ltvs,HEAD);

    void *op(CLL *lnk) {
        char *filename;
        LTVR *ltvr=(LTVR *) lnk;
        TYPE_INFO *type_info=(TYPE_INFO *) ltvr->ltv->data;
        graph_module_to_file(FORMATA(filename,256,"/tmp/CU/%x.dot",type_info),ltvr->ltv);
        return NULL;
    }
    CLL_map(&cus,FWD,op);
    CLL_release(&cus,LTVR_release);
 done:
    return status;
}


int link_base_types(LTV *dies)
{
    int status=0;
    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        int status=0;
        if ((*ltv) && !(*lti) && (!(*ltvr) || (*ltvr)->ltv==(*ltv)))
            if ((*ltv)->flags&LT_AVIS)
                *flags|=LT_TRAVERSE_HALT;
            else if ((*ltv)->flags&LT_CVAR) {
                TYPE_INFO *type_info=(TYPE_INFO *) (*ltv)->data;
                if (type_info->flags&TYPEF_BASE) {
                    char *base_name=NULL;
                    LTV *base_ltv=NULL;
                    STRY(!FORMATA(base_name,32,"%x",type_info->base),"FORMATA'ing base name");
                    STRY(!(base_ltv=LT_get(dies,base_name,HEAD)),"looking up base");
                    LT_put((*ltv),"base",HEAD,base_ltv);
                }
            }
    done:
        return status?NON_NULL:NULL;
    }
    
    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,dies,HEAD);
    STRY(listree_traverse(&ltvs,preop,NULL)!=NULL,"traversing module in postprocess");
    LTV_deq(&ltvs,HEAD);
 done:
    return status;
}


int qualify(TYPE_INFO *type_info) {
    int status=0;
    switch (type_info->tag) {
        case DW_TAG_variable:
        case DW_TAG_subprogram:
            if (!(type_info->flags&TYPEF_EXTERNAL))
                type_info->flags|=TYPEF_DQ;
        default:
            break;
    }
 done:
    return status;
}


int dwarf2edict_fd(int filedesc,LTV *mod_ltv)
{
    int status=0;
    Dwarf_Debug dbg = NULL;
    Dwarf_Error error;

    int read_cu_list()
    {
        int populate_type_info(Dwarf_Die die,TYPE_INFO *type_info)
        {
            int status=0;
            Dwarf_Off die_offset=0;
            Dwarf_Signed vint;
            Dwarf_Unsigned vuint;
            Dwarf_Addr vaddr;
            Dwarf_Off voffset;
            Dwarf_Half vshort;
            Dwarf_Bool vbool;
            Dwarf_Ptr vptr;
            Dwarf_Block *vblock;
            Dwarf_Signed atcnt=0;
            Dwarf_Attribute *atlist=NULL;
            Dwarf_Sig8 vsig8;
            const char *vcstr;
            char *vstr;

            char *diename = NULL;
            const char *tagname = 0;

            STRY(die==NULL,"testing for null die");
            STRY(dwarf_dieoffset(die,&type_info->id,&error),"getting global die offset");
            STRY(dwarf_tag(die,&type_info->tag,&error),"getting die tag");
            STRY(dwarf_diename(die,&diename,&error)==DW_DLV_ERROR,"checking dwarf_diename");
            type_info->name=diename?bufdup(diename,-1):NULL;
            dwarf_dealloc(dbg,diename,DW_DLA_STRING);

            switch (type_info->tag)
            {
                case DW_TAG_compile_unit:
                    printf("compile_unit %s\n",type_info->name);
                case DW_TAG_base_type:
                case DW_TAG_volatile_type:
                case DW_TAG_typedef:
                case DW_TAG_const_type:
                case DW_TAG_pointer_type:
                case DW_TAG_array_type:
                case DW_TAG_subrange_type:
                case DW_TAG_structure_type:
                case DW_TAG_union_type:
                case DW_TAG_member:
                case DW_TAG_enumeration_type:
                case DW_TAG_enumerator:
                case DW_TAG_subprogram:
                case DW_TAG_subroutine_type:
                case DW_TAG_formal_parameter:
                case DW_TAG_variable:
                case DW_TAG_unspecified_parameters: // varargs
                    break;
                case DW_TAG_lexical_block:
                case DW_AT_GNU_all_tail_call_sites:
                case DW_TAG_label:
                case DW_TAG_inlined_subroutine:
                case DW_TAG_GNU_call_site:
                case DW_TAG_GNU_call_site_parameter:
                    
                case DW_TAG_dwarf_procedure:
                case DW_TAG_reference_type: // C++?
                case DW_TAG_namespace:
                case DW_TAG_class_type:
                case DW_TAG_inheritance:
                case DW_TAG_imported_declaration:
                case DW_TAG_template_type_parameter:
                case DW_TAG_template_value_parameter:
                case DW_TAG_imported_module:
                    
                    goto done; // explicitly skipped
                default:
                    printf(CODE_RED "Unrecognized tag 0x%x\n" CODE_RESET,type_info->tag);
                    goto done;
            }

            TRY(dwarf_attrlist(die,&atlist,&atcnt,&error),"getting die attrlist");
            CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for DW_DLV_NO_ENTRY in dwarf_attrlist");
            SCATCH("getting die attrlist");

            Dwarf_Attribute *attr=NULL;
            while (atcnt--) {
                char *prefix;
                attr=&atlist[atcnt];

                STRY(dwarf_whatattr(*attr,&vshort,&error),"getting attr type");

                switch (vshort) {
                    case DW_AT_name: // string
                        break;
                    case DW_AT_type: // global_formref
                        IF_OK(dwarf_global_formref(*attr,&type_info->base,&error),type_info->flags|=TYPEF_BASE);
                        break;
                    case DW_AT_sibling: // global_formref
                        IF_OK(dwarf_global_formref(*attr,&type_info->sibling,&error),type_info->flags|=TYPEF_SIBLING);
                        break;
                    case DW_AT_low_pc:
                        IF_OK(dwarf_formsdata(*attr,&type_info->low_pc,&error),type_info->flags|=TYPEF_LOWPC);
                        break;
                   case DW_AT_data_member_location: // sdata
                        IF_OK(dwarf_formsdata(*attr,&type_info->data_member_location,&error),type_info->flags|=TYPEF_MEMBERLOC);
                        break;
                    case DW_AT_const_value: // sdata
                        IF_OK(dwarf_formsdata(*attr,&type_info->const_value,&error),type_info->flags|=TYPEF_CONSTVAL);
                        break;
                    case DW_AT_location: // sdata
                        IF_OK(dwarf_formsdata(*attr,&type_info->location,&error),type_info->flags|=TYPEF_LOCATION);
                        break;
                    case DW_AT_byte_size:
                        IF_OK(dwarf_formudata(*attr,&type_info->bytesize,&error),type_info->flags|=TYPEF_BYTESIZE);
                        break;
                    case DW_AT_bit_offset:
                        IF_OK(dwarf_formudata(*attr,&type_info->bitoffset,&error),type_info->flags|=TYPEF_BITOFFSET);
                        break;
                    case DW_AT_bit_size:
                        IF_OK(dwarf_formudata(*attr,&type_info->bitsize,&error),type_info->flags|=TYPEF_BITSIZE);
                        break;
                    case DW_AT_external:
                        IF_OK(dwarf_formflag(*attr,&type_info->external,&error),type_info->flags|=TYPEF_EXTERNAL);
                        break;
                    case DW_AT_upper_bound:
                        IF_OK(dwarf_formsdata(*attr,&type_info->upper_bound,&error),type_info->flags|=TYPEF_UPPERBOUND);
                        break;
                    case DW_AT_encoding: // DW_ATE_unsigned, etc.
                        IF_OK(dwarf_formsdata(*attr,&type_info->encoding,&error),type_info->flags|=TYPEF_ENCODING);
                        break;
                    case DW_AT_high_pc:
                    case DW_AT_decl_line:
                    case DW_AT_decl_file:
                    case DW_AT_call_line:
                    case DW_AT_call_file:
                    case DW_AT_GNU_all_tail_call_sites:
                    case DW_AT_GNU_all_call_sites:
                    case DW_AT_stmt_list:
                    case DW_AT_comp_dir:
                    case DW_AT_static_link:
                    case DW_AT_artificial: // __line__, etc.
                    case DW_AT_frame_base: // stack?
                    case DW_AT_inline:
                    case DW_AT_prototyped: // signature?
                    case DW_AT_language:
                    case DW_AT_producer:
                    case DW_AT_declaration: // i.e. not a definition
                    case DW_AT_abstract_origin: // associated with DW_TAG_inlined_subroutine
                    case DW_AT_GNU_tail_call:
                    case DW_AT_GNU_call_site_value:
                    case 8473: // an attribute that has no definition or name in current dwarf.h
                        
                    case DW_AT_specification: // C++?
                    case DW_AT_object_pointer: // C++
                    case DW_AT_pure: // C++
                    case DW_AT_linkage_name: // C++ mangling?
                    case DW_AT_accessibility:
                    case DW_AT_ranges:
                    case DW_AT_explicit:
                        
                        break;
                    default:
                        printf(CODE_RED "Unrecognized attr 0x%x\n",vshort);
                        break;
                }

                /*
                  STRY(dwarf_whatform(*attr,&vshort,&error),"getting attr form");
                  STRY(dwarf_get_FORM_name(vshort,&vcstr),"getting attr formname");
                  printf("form %d (%s) ",vshort,vcstr);

                  STRY(dwarf_whatform_direct(*attr,&vshort,&error),"getting attr form_direct");
                  STRY(dwarf_get_FORM_name(vshort,&vcstr),"getting attr form_direct name");
                  printf("form_direct %d (%s) ",vshort,vcstr);

                IF_OK(dwarf_formref(*attr,&voffset,&error),       printf("formref 0x%"        DW_PR_DSx " ",voffset));
                IF_OK(dwarf_global_formref(*attr,&voffset,&error),printf("global_formref 0x%" DW_PR_DSx " ",voffset));
                IF_OK(dwarf_formaddr(*attr,&vaddr,&error),        printf("addr 0x%"           DW_PR_DUx " ",vaddr));
                IF_OK(dwarf_formflag(*attr,&vbool,&error),        printf("flag %"             DW_PR_DSd " ",vbool));
                IF_OK(dwarf_formudata(*attr,&vuint,&error),       printf("udata %"            DW_PR_DUu " ",vuint));
                IF_OK(dwarf_formsdata(*attr,&vint,&error),        printf("sdata %"            DW_PR_DSd " ",vint));
                IF_OK(dwarf_formblock(*attr,&vblock,&error),      printf("block 0x%"          DW_PR_DUx " ",vblock->bl_len));
                IF_OK(dwarf_formstring(*attr,&vstr,&error),       printf("string %s ",                      vstr));
                IF_OK(dwarf_formsig8(*attr,&vsig8,&error),        printf("addr %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x ",
                                                                         vsig8.signature[0],vsig8.signature[1],vsig8.signature[2],vsig8.signature[3],
                                                                         vsig8.signature[4],vsig8.signature[5],vsig8.signature[6],vsig8.signature[7]));
                */

                int get_expr_loclist_data(Dwarf_Unsigned exprlen,Dwarf_Ptr exprloc) {
                    int status=0;
                    Dwarf_Locdesc *llbuf;
                    Dwarf_Signed listlen;
                    STRY(dwarf_loclist_from_expr(dbg,exprloc,exprlen,&llbuf,&listlen,&error),"getting exprloc");
                    for (int j=0;j<llbuf->ld_cents;j++)
                    {
                        if (llbuf->ld_s[j].lr_atom >= DW_OP_breg0 && llbuf->ld_s[j].lr_atom <= DW_OP_breg31) ;
                        else if (llbuf->ld_s[j].lr_atom >= DW_OP_reg0 && llbuf->ld_s[j].lr_atom <= DW_OP_reg31) ;
                        else if (llbuf->ld_s[j].lr_atom >= DW_OP_lit0 && llbuf->ld_s[j].lr_atom <= DW_OP_lit31) ;
                        else
                            switch(llbuf->ld_s[j].lr_atom)
                            {
                                case DW_OP_addr:
                                    type_info->addr=llbuf->ld_s[j].lr_number;
                                    type_info->flags|=TYPEF_ADDR;
                                    break;
                                case DW_OP_consts: case DW_OP_const1s: case DW_OP_const2s: case DW_OP_const4s: case DW_OP_const8s: // (Dwarf_Signed) llbuf->ld_s[j].lr_number
                                case DW_OP_constu: case DW_OP_const1u: case DW_OP_const2u: case DW_OP_const4u: case DW_OP_const8u: // llbuf->ld_s[j].lr_number
                                case DW_OP_fbreg: // (Dwarf_Signed) llbuf->ld_s[j].lr_number
                                case DW_OP_bregx: // printf(" bregx %" DW_PR_DUu " + (%" DW_PR_DSd ") ",llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2);
                                case DW_OP_regx: // printf(" regx %" DW_PR_DUu " + (%" DW_PR_DSd ") ",llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2);
                                case DW_OP_pick:
                                case DW_OP_plus_uconst:
                                case DW_OP_piece:
                                case DW_OP_deref_size:
                                case DW_OP_xderef_size:
                                case DW_OP_GNU_uninit:
                                case DW_OP_GNU_encoded_addr:
                                case DW_OP_GNU_implicit_pointer:
                                case DW_OP_GNU_entry_value:
                                case DW_OP_GNU_push_tls_address: // something to do with stack local variables?
                                case DW_OP_call_frame_cfa:
                                case DW_OP_deref:
                                case DW_OP_skip:
                                case DW_OP_bra:
                                case DW_OP_plus:
                                case DW_OP_shl:
                                case DW_OP_or:
                                case DW_OP_and:
                                case DW_OP_xor:
                                case DW_OP_eq:
                                case DW_OP_ne:
                                case DW_OP_gt:
                                case DW_OP_lt:
                                case DW_OP_shra:
                                case DW_OP_mul:
                                case DW_OP_minus:
                                    
                                case DW_OP_stack_value: // 0x9f
                                case DW_OP_lit16: // 0x40
                                    
                                    // printf(" Ingnored DW_OP 0x%x n 0x%x n2 0x%x offset 0x%x",llbuf->ld_s[j].lr_atom,llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2,llbuf->ld_s[j].lr_offset);
                                    break;
                                default:
                                    printf(" Unrecognized DW_OP 0x%x n 0x%x n2 0x%x offset 0x%x",llbuf->ld_s[j].lr_atom,llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2,llbuf->ld_s[j].lr_offset);
                                    printf(CODE_RED " lowpc %" DW_PR_DUx " hipc %"  DW_PR_DUx " ld_section_offset %" DW_PR_DUx " ld_from_loclist %s ld_cents %d ",
                                           llbuf->ld_lopc,llbuf->ld_hipc,llbuf->ld_section_offset,llbuf->ld_from_loclist?"debug_loc":"debug_info",llbuf->ld_cents);
                                    printf(CODE_RESET "\n");
                                    break;
                            }
                    }
                    dwarf_dealloc(dbg,llbuf->ld_s, DW_DLA_LOC_BLOCK);
                    dwarf_dealloc(dbg,llbuf, DW_DLA_LOCDESC);
                done:
                    return status;
                }

                IF_OK(dwarf_formexprloc(*attr,&vuint,&vptr,&error),get_expr_loclist_data(vuint,vptr));
                // printf(CODE_RESET "\n");

                dwarf_dealloc(dbg,atlist[atcnt],DW_DLA_ATTR);
            }
            dwarf_dealloc(dbg,atlist,DW_DLA_LIST);

        done:
            return status;
        }

        int status=0;
        LTV *dies=NULL;

        int process_type_node(LTV *parent,Dwarf_Die die)
        {
            int status=0;
            LTV *ltv=NULL;

            int traverse_child()
            {
                int status=0;
                Dwarf_Die child=0;
                TRY(dwarf_child(die,&child,&error),"checking dwarf_child");
                CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking dwarf child");
                SCATCH("checking dwarf_child");
                STRY(process_type_node(ltv,child),"getting child/sib dies");
            done:
                return status;
            }

            int traverse_sibling()
            {
                int status=0;
                Dwarf_Die sibling=0;
                TRY(dwarf_siblingof(dbg,die,&sibling,&error),"checking dwarf_sibling");
                CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for DW_DLV_NO_ENTRY"); /* Done at this level. */
                SCATCH("checking dwarf_siblingof");
                STRY(process_type_node(parent,sibling),"getting child/sib dies");
            done:
                return status;
            }

            int link2parent(TYPE_INFO *type_info) {
                TYPE_INFO *pti=(TYPE_INFO *) parent->data;
                if (!type_info->name)
                    return false;
                if (pti->tag==DW_TAG_compile_unit)
                    switch(type_info->tag) {
                        case DW_TAG_subprogram:
                        case DW_TAG_variable:
                            return true;
                        default:
                            return false;
                    }
                return true;
            }

            if (die) // no die implies we're at the top layer, just traverse sibs
            {
                TYPE_INFO *type_info=NULL;
                STRY(!parent,"checking parent");
                STRY(!(type_info=NEW(TYPE_INFO)),"allocating type_info");
                STRY(populate_type_info(die,type_info),"populating type_info");
                STRY(qualify(type_info),"qualifying type info");
                if (type_info->flags&TYPEF_DQ)
                    DELETE(type_info);
                else
                {
                    STRY(!(ltv=LTV_new(type_info,sizeof(TYPE_INFO),LT_OWN | LT_CVAR)),"allocating type_info ltv");
                    char *idbuf=FORMATA(idbuf,32,"%x",type_info->id);
                    LT_put(dies,idbuf,HEAD,ltv);
                    if (link2parent(type_info))
                        LT_put(parent,type_info->name,HEAD,ltv);
                    STRY(traverse_child(),"traversing first die child");
                }
            }

            status=traverse_sibling();

        done:
            if (die)
                dwarf_dealloc(dbg,die,DW_DLA_DIE);
            return status;
        }

        Dwarf_Unsigned cu_header_length = 0;
        Dwarf_Half version_stamp = 0;
        Dwarf_Unsigned abbrev_offset = 0;
        Dwarf_Half address_size = 0;
        Dwarf_Unsigned next_cu_header = 0;
        Dwarf_Half length_size = 0;
        Dwarf_Half extension_size = 0;

        STRY(!(dies=LTV_VOID),"allocating ltv for module dies");

        while (1)
        {
            TRY(dwarf_next_cu_header_b(dbg,&cu_header_length,&version_stamp,&abbrev_offset,&address_size,&length_size,&extension_size,&next_cu_header,&error),"reading next cu header");
            CATCH(status==DW_DLV_NO_ENTRY,0,goto finished,"checking for no next cu header");
            CATCH(status==DW_DLV_ERROR,status,goto done,"checking error dwarf_next_cu_header");
            STRY(process_type_node(mod_ltv,NULL),"processing type node"); // get siblings of CU header
        }
    finished:
        STRY(link_base_types(dies),"postprocessing module");
        LTV_release(dies);
        graph_module_to_file("/tmp/module.dot",mod_ltv);
        graph_cus_to_files(mod_ltv);
    done:
        return status;
    }

    STRY(dwarf_init(filedesc,DW_DLC_READ,NULL,NULL,&dbg,&error),"initializing dwarf reader");
    TRYCATCH(read_cu_list(),status,close_dwarf,"reading cu list");
 close_dwarf:
    STRY(dwarf_finish(dbg,&error),"finalizing dwarf reader");
 done:
    return status;
}


int import_module(char *filename,LTV *mod_ltv)
{
    int status=0;
    int filedesc = -1;
    STRY((filedesc=open(filename,O_RDONLY))<0,"opening dward2edict input file %s",filename);
    TRYCATCH(dwarf2edict_fd(filedesc,mod_ltv),status,close_file,"importing dwarf from filedesc");
 close_file:
    close(filedesc);
 done:
    return status;
}


/*
  int dwarf2edict(char *filename)
  {
  int status=0;
  int import_fd = -1;
  Dwarf_Debug dbg = NULL;
  Dwarf_Error error;

  STRY((import_fd=open(filename,O_RDONLY))<0,"opening dward2edict input file %s",filename);

    TRYCATCH(dwarf_init(import_fd,DW_DLC_READ,NULL,NULL,&dbg,&error),status,close_file,"initializing dwarf reader");
    read_cu_list();
    TRYCATCH(dwarf_finish(dbg,&error),status,close_file,"finalizing dwarf reader");

    close_file:
    close(import_fd);
    done:
    return status;
}
*/


#if 0
#define ENUMS_PREFIX "" //"enums."

DICT_ITEM *SU_subitem(DICT *dict,char *name) { return jli_getitem(dict,name,strlen(name),0); }
char *SU_lookup(DICT *dict,char *name) { return jli_lookup(dict,name,strlen(name)); }


DICT_ITEM *Type_getTypeInfo(DICT_ITEM *item,TYPE_INFO *type_info);
DICT_ITEM *Type_isaTypeItem(DICT *dict,char *type_id);
DICT_ITEM *Type_lookupName(DICT *dict,char *name);

void Type_dump(DICT *dict,DICT_ITEM *typeitem,char *addr,void *data);
void Type_dumpType(DICT *dict,char *name,char *addr,char *prefix);
void Type_dumpVar(DICT *dict,char *name,char *prefix);



typedef struct
{
    DICT_ITEM *typeitem;
    char *addr;
} TYPE_VAR;

DICT_ITEM *Type_getTypeInfo(DICT_ITEM *typeitem,TYPE_INFO *type_info)
{
    if (typeitem && type_info)
    {
        ull *addr;
        char *p;
        if ((p = typeitem->name))
            type_info->type_id = p;
        if ((p = SU_lookup(&typeitem->dict,"type") + 7))
            type_info->category = p; // skip "DW_TAG_"
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_name")))
            type_info->DW_AT_name = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_type")))
            type_info->DW_AT_type = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_const_value")))
            type_info->DW_AT_const_value = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_byte_size")))
            type_info->DW_AT_byte_size = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_bit_size")))
            type_info->DW_AT_bit_size = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_bit_offset")))
            type_info->DW_AT_bit_offset = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_data_member_location")))
            type_info->DW_AT_data_member_location = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_location")))
            type_info->DW_AT_location = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_encoding")))
            type_info->DW_AT_encoding = p;
        if ((p = SU_lookup(&typeitem->dict,"DW_AT_upper_bound")))
            type_info->DW_AT_upper_bound = p;

        type_info->index = strtou(SU_lookup(&typeitem->dict,"index"));
        type_info->level = strtou(SU_lookup(&typeitem->dict,"level"));
        type_info->item = typeitem;
        type_info->children = SU_subitem(&typeitem->dict,"children");
        if (!type_info->name)
            type_info->name="";

        type_info->typename=SU_lookup(&typeitem->dict,"typename");
        type_info->nexttype=(addr=STRTOULLP(SU_lookup(&typeitem->dict,"nexttype")))?(DICT_ITEM *)*addr:NULL;
    }

    return typeitem;
}

DICT_ITEM *Type_findBasic(DICT *dict,DICT_ITEM *typeitem,TYPE_INFO *type_info)
{
    while (typeitem && type_info && Type_getTypeInfo(typeitem,type_info))
    {
        if (!strcmp(type_info->category,"base_type") ||
            !strcmp(type_info->category,"enumeration_type") ||
            !strcmp(type_info->category,"structure_type") ||
            !strcmp(type_info->category,"union_type") ||
            !strcmp(type_info->category,"pointer_type") ||
            !strcmp(type_info->category,"array_type"))
            break;

        typeitem=type_info->nexttype;
    }

    return typeitem;
}


DICT_ITEM *Type_combine(TYPE_INFO *type_info,char *addr,char *name)
{
    type_info->addr=addr;
    type_info->name=name;
    return type_info->item;
}

#define COND_ADD_PARAM(PARAM,ROOT,LABEL)                              \
    if (type_info.PARAM)                                              \
    {                                                                 \
        char *name = CONCATA(name,ROOT,"." LABEL);                    \
        jli_install(type_info.PARAM,name);                            \
    }


void Type_install(DICT *dict,char *val,char *category,char *name,char *subname)
{
    char *newname,*p;
    if (val && category && name && subname)
    {
        newname=FORMATA(p,256,"%s%s%s",category,name,subname);
        if (!jli_lookup(dict,newname,strlen(newname)))
            jli_install(dict,val,newname);
    }
}


DICT_ITEM *Type_isaTypeItem(DICT *dict,char *type_id)
{
    if (type_id)
    {
        char *format=type_id[0]=='t'?"types.":"types.t";
        char *isa_type=type_id? CONCATA(isa_type,format,type_id):NULL;
        return isa_type? jli_getitem(dict,isa_type,strlen(isa_type),0):NULL;
    }
    return NULL;
}

#define TYPE_NAMEDICT "type_names."

DICT_ITEM *Type_lookupName(DICT *dict,char *name)
{
    if (name)
    {
        char *typename=CONCATA(typename,TYPE_NAMEDICT,name);
        ull *itemref=INTVAR(dict,typename,0);
        return itemref?(DICT_ITEM *) *itemref:NULL;
    }
    else
        return NULL;
}


long long *Type_getLocation(char *loc)
{
    return STRTOLLP(loc);
}


void *Type_remapChildren(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
{
    void *Type_remapChild(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
    {
        DICT_ITEM *typeitem=Type_isaTypeItem(dict,item->name);
        if (typeitem)
            Type_install(dict,ulltostr("0x%llx",(ull) typeitem),(char *) data,".children.",item->data);
        return NULL;
    }

    DICT_ITEM *children=jli_getitem(&item->dict,"children",8,1);
    if (children)
    {
        char *p;
        CONCATA(p,"types.",item->name);
        dict_traverse(dict,&children->dict,Type_remapChild,p);
        DELETE(dict_free(children)); // pop/dict_free/delete children
    }
    return NULL;
}


void *Type_mapEnum(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
{
    void *Type_mapEnumeration(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
    {
        TYPE_INFO *parent=(TYPE_INFO *) data;
        ull *addr=STRTOULLP(item->data);
        if (addr)
        {
            DICT_ITEM *typeitem=(DICT_ITEM *) *addr;
            TYPE_INFO type_info;
            Type_getTypeInfo(typeitem,&ZERO(type_info));
            Type_install(&parent->item->dict,type_info.DW_AT_name,"values.",type_info.DW_AT_const_value,"");
            Type_install(dict,type_info.DW_AT_const_value,ENUMS_PREFIX,type_info.DW_AT_name,"");
        }
        return NULL;
    }

    ull *addr=STRTOULLP(item->data);
    if (addr)
    {
        TYPE_INFO type_info;
        Type_getTypeInfo((DICT_ITEM *) *addr,&ZERO(type_info));
        if (type_info.children)
            dict_traverse(dict,&type_info.children->dict,Type_mapEnumeration,&type_info);
    }

    return NULL;
}


DICT_ITEM *Type_getChild(DICT *dict,DICT_ITEM *typeitem,char *member,int n)
{
    void *Type_findMemberByName(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
    {
        ull *addr=STRTOULLP(item->data);
        if (addr)
        {
            DICT_ITEM *typeitem=(DICT_ITEM *) *addr;
            char *member=(char *) data;
            int memberlen=hdict_delimit(member,strlen(member));
            char *name=SU_lookup(&typeitem->dict,"DW_AT_name");
            if (name && strlen(name)==memberlen && !strncmp(name,member,memberlen))
                return typeitem;
        }
        return NULL;
    }

    void *Type_findMemberByIndex(DICT *dict,char *name)
    {
        DICT_ITEM *item=jli_getitem(dict,name,strlen(name),0);
        ull *addr=item?STRTOULLP(item->data):NULL;
        return addr?(void *) *addr:NULL;
    }

    DICT_ITEM *children=NULL;

    if (typeitem && (children=SU_subitem(&typeitem->dict,"children")))
    {
        typeitem=member?
            dict_traverse(dict,&children->dict,Type_findMemberByName,(void *) member):
            Type_findMemberByIndex(&children->dict,(char *) ulltostr("%llu",(ull)n));
    }

    return typeitem;
}

void Type_mapTypeInfoTypes(DICT *dict,TYPE_INFO *type_info)
{
    if (type_info->DW_AT_type &&
        (type_info->nexttype=Type_isaTypeItem(dict,type_info->DW_AT_type)))
        Type_install(dict,ulltostr("0x%llx",(ull) type_info->nexttype),"types.",type_info->type_id,".nexttype");
}

void Type_mapTypeInfoNames(DICT *dict,TYPE_INFO *type_info)
{
    char *format=NULL,*p=NULL;
    int indirect=0;

    if (!strcmp(type_info->category,"base_type")) format="%s";
    else if (!strcmp(type_info->category,"typedef")) format="%s";
    else if (!strcmp(type_info->category,"enumeration_type")) format="enum.%s";
    else if (!strcmp(type_info->category,"structure_type")) format="struct.%s";
    else if (!strcmp(type_info->category,"union_type")) format="union %s";
    else if (!strcmp(type_info->category,"variable") && type_info->level==1) format="variable.%s";
    else if (!strcmp(type_info->category,"subprogram") && type_info->level==1) format="subprogram.%s";
    //else if (!strcmp(type_info->category,"subroutine_type") && Type_isaTypeItem(dict,type_info->DW_AT_type))
    else
    {
        TYPE_INFO next_type_info;
        Type_getTypeInfo(type_info->nexttype,&ZERO(next_type_info));
        indirect=1;

        if (!strcmp(type_info->category,"subroutine_type"))
        {
            format="function.%s";
            type_info->DW_AT_name=type_info->nexttype?next_type_info.typename:"anonsub";
        }
        else if (!strcmp(type_info->category,"pointer_type"))
        {
            format="pointer.%s";
            type_info->DW_AT_name=type_info->nexttype?next_type_info.typename:"void";
        }
        else if (!strcmp(type_info->category,"volatile_type"))
        {
            format="volatile.%s";
            type_info->DW_AT_name=type_info->nexttype?next_type_info.typename:"anonvol";
        }
        else if (!strcmp(type_info->category,"const_type"))
        {
            format="const.%s";
            type_info->DW_AT_name=type_info->nexttype?next_type_info.typename:"anonconst";
        }
        else if (!strcmp(type_info->category,"array_type"))
        {
            TYPE_INFO subrange_info,element_info;
            DICT_ITEM *subrange_item=Type_getTypeInfo(Type_getChild(dict,type_info->item,NULL,0),&ZERO(subrange_info));
            ull *upper_bound,*byte_size;
            FORMATA(format,64,"array.%%s.[%s]",subrange_info.DW_AT_upper_bound);

            if (next_type_info.typename && Type_findBasic(dict,type_info->nexttype,&ZERO(element_info)))
            {
                ull *upper_bound=STRTOULLP(subrange_info.DW_AT_upper_bound);
                ull *byte_size=STRTOULLP(element_info.DW_AT_byte_size);
                ull bs = upper_bound && byte_size? ((*byte_size)*((*upper_bound)+1)):0;

                if (!type_info->DW_AT_byte_size)
                    Type_install(dict,ulltostr("0x%llx",(ull)bs),"types.",type_info->type_id,".DW_AT_byte_size");

                type_info->DW_AT_name=next_type_info.typename;
            }
        }
    }

    if (format)
    {
        char *itemstr=ulltostr("0x%llx",(ull) type_info->item);

        if (type_info->DW_AT_name)
        {
            FORMATA(p,256,format,type_info->DW_AT_name);
            Type_install(dict,p,"types.",type_info->type_id,".typename");
            Type_install(dict,itemstr,TYPE_NAMEDICT,p,"");
        }
        else if (!indirect)
        {
            FORMATA(p,256,format,type_info->type_id);
            Type_install(dict,p,"types.",type_info->type_id,".typename");
            Type_install(dict,itemstr,TYPE_NAMEDICT,p,"");
        }
        else if (indirect)
        {
            Type_install(dict,"","unresolved.",itemstr,"");
        }
    }
}

void *Type_mapTypeInfo(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
{
    TYPE_INFO type_info;

    if (Type_getTypeInfo(item,&ZERO(type_info)))
    {
        Type_mapTypeInfoTypes(dict,&type_info);
        Type_mapTypeInfoNames(dict,&type_info);
    }
    return NULL;
}


void *Type_mapUnresolved(DICT *dict,DICT_ITEM *iter,DICT_ITEM *item,void *data)
{
    TYPE_INFO type_info;
    ull *addr=STRTOULLP(item->name);
    if (addr)
    {
        Type_getTypeInfo((DICT_ITEM *) *addr,&ZERO(type_info));
        Type_mapTypeInfoNames(dict,&type_info);
    }
    return NULL;
}

void Type_resolve(DICT *dict)
{
    DICT_ITEM *unresolved;
    while ((unresolved=jli_getitem(dict,"unresolved",10,1)))
    {
        dict_traverse(dict,&unresolved->dict,Type_mapUnresolved,NULL);
        DELETE(dict_free(unresolved));
    }
}


void Type_permute(DICT *dict,char *name)
{
    DICT_ITEM *item = jli_getitem(dict,name,strlen(name),0);
    if (item)
    {
        DICT_ITEM *enums=NULL;
        DICT_ITEM *structs=NULL;
        DICT_ITEM *unions=NULL;

        printf("    Remapping Children...\n");
        dict_traverse(dict,&item->dict,Type_remapChildren,"");
        printf("    Mapping TypeInfo...\n");
        dict_traverse(dict,&item->dict,Type_mapTypeInfo,"");
        printf("    Resolving...\n");
        Type_resolve(dict);
        printf("    Mapping Enums...\n");
        enums=jli_getitem(dict,"type_names.enum",15,0);
        if (enums) dict_traverse(dict,&enums->dict,Type_mapEnum,"");
    }
}


static char *Type_oformat[] = { "%s","%d","%d","%d","%lld","0x%x","0x%x","0x%x","0x%llx","%g","%g","%Lg" };
static char *Type_iformat[] = { "%s","%i","%i","%i","%lli","%i","%i","%i","%lli","%g","%g","%Lg" };

char *Type_pushUVAL(TYPE_UVALUE *uval,char *buf)
{
    switch(uval->dutype)
    {
        case TYPE_INT1S:   sprintf(buf,Type_oformat[uval->dutype],uval->int1s.val); break;
        case TYPE_INT2S:   sprintf(buf,Type_oformat[uval->dutype],uval->int2s.val); break;
        case TYPE_INT4S:   sprintf(buf,Type_oformat[uval->dutype],uval->int4s.val); break;
        case TYPE_INT8S:   sprintf(buf,Type_oformat[uval->dutype],uval->int8s.val); break;
        case TYPE_INT1U:   sprintf(buf,Type_oformat[uval->dutype],uval->int1u.val); break;
        case TYPE_INT2U:   sprintf(buf,Type_oformat[uval->dutype],uval->int2u.val); break;
        case TYPE_INT4U:   sprintf(buf,Type_oformat[uval->dutype],uval->int4u.val); break;
        case TYPE_INT8U:   sprintf(buf,Type_oformat[uval->dutype],uval->int8u.val); break;
        case TYPE_FLOAT4:  sprintf(buf,Type_oformat[uval->dutype],uval->float4.val); break;
        case TYPE_FLOAT8:  sprintf(buf,Type_oformat[uval->dutype],uval->float8.val); break;
        case TYPE_FLOAT12: sprintf(buf,Type_oformat[uval->dutype],uval->float12.val); break;
        default:           buf[0]=0; break;
    }
    return buf;
}

TYPE_UVALUE *Type_pullUVAL(TYPE_UVALUE *uval,char *buf)
{
    int tVar;
    switch(uval->dutype)
    {
        case TYPE_INT1S:   sscanf(buf,Type_iformat[uval->dutype],&tVar);uval->int1s.val=tVar; break;
        case TYPE_INT2S:   sscanf(buf,Type_iformat[uval->dutype],&tVar);uval->int2s.val=tVar; break;
        case TYPE_INT4S:   sscanf(buf,Type_iformat[uval->dutype],&tVar);uval->int4s.val=tVar; break;
        case TYPE_INT8S:   sscanf(buf,Type_iformat[uval->dutype],&uval->int8s.val); break;
        case TYPE_INT1U:   sscanf(buf,Type_iformat[uval->dutype],&tVar);uval->int1u.val=tVar; break;
        case TYPE_INT2U:   sscanf(buf,Type_iformat[uval->dutype],&tVar);uval->int2u.val=tVar; break;
        case TYPE_INT4U:   sscanf(buf,Type_iformat[uval->dutype],&tVar);uval->int4u.val=tVar; break;
        case TYPE_INT8U:   sscanf(buf,Type_iformat[uval->dutype],&uval->int8u.val); break;
        case TYPE_FLOAT4:  sscanf(buf,Type_iformat[uval->dutype],&uval->float4.val); break;
        case TYPE_FLOAT8:  sscanf(buf,Type_iformat[uval->dutype],&uval->float8.val); break;
        case TYPE_FLOAT12: sscanf(buf,Type_iformat[uval->dutype],&uval->float12.val); break;
        default:           buf[0]=0; break;
    }
    return uval;
}


#define UVAL2VAR(uval,var)                                                      \
    {                                                                           \
        switch (uval.dutype)                                                    \
        {                                                                       \
            case TYPE_INT1S: var=(typeof(var)) uval.int1s.val; break;           \
            case TYPE_INT2S: var=(typeof(var)) uval.int2s.val; break;           \
            case TYPE_INT4S: var=(typeof(var)) uval.int4s.val; break;           \
            case TYPE_INT8S: var=(typeof(var)) uval.int8s.val; break;           \
            case TYPE_INT1U: var=(typeof(var)) uval.int1u.val; break;           \
            case TYPE_INT2U: var=(typeof(var)) uval.int2u.val; break;           \
            case TYPE_INT4U: var=(typeof(var)) uval.int4u.val; break;           \
            case TYPE_INT8U: var=(typeof(var)) uval.int8u.val; break;           \
            case TYPE_FLOAT4: var=(typeof(var)) uval.float4.val; break;         \
            case TYPE_FLOAT8: var=(typeof(var)) uval.float8.val; break;         \
            case TYPE_FLOAT12: var=(typeof(var)) uval.float12.val; break;       \
        }                                                                       \
    }

#define GETUVAL(member,type,uval)                                               \
    {                                                                           \
        uval->member.dutype = type;                                             \
        uval->member.val = *(typeof(uval->member.val) *) addr;                  \
    }

#define GETUBITS(member,type,uval,bsize,boffset,issigned)                       \
    {                                                                           \
        GETUVAL(member,type,uval);                                              \
                                                                                \
        if (bsize && boffset)                                                   \
        {                                                                       \
            int shift = (sizeof(uval->member.val)*8)-(*bsize)-(*boffset);       \
            typeof(uval->member.val) mask = (1<<*bsize)-1;                      \
            uval->member.val = (uval->member.val >> shift) & mask;              \
            if (issigned && (uval->member.val & (1<<*bsize-1)))                 \
                uval->member.val |= ~mask;                                      \
        }                                                                       \
    }

TYPE_UTYPE Type_getUVAL(TYPE_INFO *type_info,void *addr,TYPE_UVALUE *uval)
{
    ZERO(*uval);
    ull *size,*encoding,*bit_size,*bit_offset;

    if (addr && type_info && uval)
    {
        if ((!strcmp(type_info->category,"base_type") &&
             (size=STRTOULLP(type_info->DW_AT_byte_size)) &&
             (encoding=STRTOULLP(type_info->DW_AT_encoding))) ||
            (!strcmp(type_info->category,"enumeration_type") &&
             (size=STRTOULLP(type_info->DW_AT_byte_size)) &&
             (encoding=STRTOULLP("7"))) ||
            (!strcmp(type_info->category,"pointer_type") &&
             (size=STRTOULLP(type_info->DW_AT_byte_size)) &&
             (encoding=STRTOULLP("7"))))
        {
            ull *bit_size=STRTOULLP(type_info->DW_AT_bit_size);
            ull *bit_offset=STRTOULLP(type_info->DW_AT_bit_offset);

            switch (*encoding)
            {
                case DW_ATE_float:
                    if (*size==4)  GETUVAL(float4,TYPE_FLOAT4,uval);
                    if (*size==8)  GETUVAL(float8,TYPE_FLOAT8,uval);
                    if (*size==12) GETUVAL(float12,TYPE_FLOAT12,uval);
                    break;
                case DW_ATE_signed:
                case DW_ATE_signed_char:
                    if (*size==1) GETUBITS(int1s,TYPE_INT1S,uval,bit_size,bit_offset,1);
                    if (*size==2) GETUBITS(int2s,TYPE_INT2S,uval,bit_size,bit_offset,1);
                    if (*size==4) GETUBITS(int4s,TYPE_INT4S,uval,bit_size,bit_offset,1);
                    if (*size==8) GETUBITS(int8s,TYPE_INT8S,uval,bit_size,bit_offset,1);
                    break;
                case DW_ATE_unsigned:
                case DW_ATE_unsigned_char:
                    if (*size==1) GETUBITS(int1u,TYPE_INT1U,uval,bit_size,bit_offset,0);
                    if (*size==2) GETUBITS(int2u,TYPE_INT2U,uval,bit_size,bit_offset,0);
                    if (*size==4) GETUBITS(int4u,TYPE_INT4U,uval,bit_size,bit_offset,0);
                    if (*size==8) GETUBITS(int8u,TYPE_INT8U,uval,bit_size,bit_offset,0);
                    break;
                default:
                    break;
            }
        }

        return uval->dutype;
    }

    return TYPE_NONE;
}

#define PUTUVAL(member,type,uval)                                               \
    {                                                                           \
        *(typeof(uval->member.val) *) addr = uval->member.val;                  \
    }

#define PUTUBITS(member,type,uval,bsize,boffset,issigned)                       \
    {                                                                           \
        if (bsize && boffset)                                                   \
        {                                                                       \
            TYPE_UVALUE tuval;                                                  \
            GETUVAL(member,type,(&tuval));                                      \
            int shift = (sizeof(uval->member.val)*8)-(*bsize)-(*boffset);       \
            typeof(uval->member.val) mask = ((1<<*bsize)-1) << shift;           \
            uval->member.val = (uval->member.val << shift) & mask;              \
            uval->member.val |= tuval.member.val & ~mask;                       \
        }                                                                       \
                                                                                \
        PUTUVAL(member,type,uval);                                              \
    }

int Type_putUVAL(TYPE_INFO *type_info,void *addr,TYPE_UVALUE *uval)
{
    int status=0;
    ull *size,*encoding,*bit_size,*bit_offset;

    if (addr && type_info && uval)
    {
        if ((!strcmp(type_info->category,"base_type") &&
             (size=STRTOULLP(type_info->DW_AT_byte_size)) &&
             (encoding=STRTOULLP(type_info->DW_AT_encoding))) ||
            (!strcmp(type_info->category,"enumeration_type") &&
             (size=STRTOULLP(type_info->DW_AT_byte_size)) &&
             (encoding=STRTOULLP("7"))) ||
            (!strcmp(type_info->category,"pointer_type") &&
             (size=STRTOULLP(type_info->DW_AT_byte_size)) &&
             (encoding=STRTOULLP("7"))))
        {
            ull *bit_size=STRTOULLP(type_info->DW_AT_bit_size);
            ull *bit_offset=STRTOULLP(type_info->DW_AT_bit_offset);

            switch (*encoding)
            {
                case 4: // float
                    if (*size==4)  PUTUVAL(float4,TYPE_FLOAT4,uval);
                    if (*size==8)  PUTUVAL(float8,TYPE_FLOAT8,uval);
                    if (*size==12) PUTUVAL(float12,TYPE_FLOAT12,uval);
                    status=1;
                    break;
                case 5: // signed int
                case 6: // signed char
                    if (*size==1) PUTUBITS(int1s,TYPE_INT1S,uval,bit_size,bit_offset,1);
                    if (*size==2) PUTUBITS(int2s,TYPE_INT2S,uval,bit_size,bit_offset,1);
                    if (*size==4) PUTUBITS(int4s,TYPE_INT4S,uval,bit_size,bit_offset,1);
                    if (*size==8) PUTUBITS(int8s,TYPE_INT8S,uval,bit_size,bit_offset,1);
                    status=1;
                    break;
                case 7: // unsigned int
                case 8: // unsigned char
                    if (*size==1) PUTUBITS(int1u,TYPE_INT1U,uval,bit_size,bit_offset,0);
                    if (*size==2) PUTUBITS(int2u,TYPE_INT2U,uval,bit_size,bit_offset,0);
                    if (*size==4) PUTUBITS(int4u,TYPE_INT4U,uval,bit_size,bit_offset,0);
                    if (*size==8) PUTUBITS(int8u,TYPE_INT8U,uval,bit_size,bit_offset,0);
                    status=1;
                    break;
                default:
                    break;
            }
        }
    }

    return status;
}

int Type_isBitField(TYPE_INFO *type_info) { return (type_info->DW_AT_bit_size || type_info->DW_AT_bit_offset); }

#define TYPE_INFO_NAME(p,ti) (((p)=Type_isBitField(ti)?                       \
                               FORMATA((p),256,"%s:%s:%s",(ti).typename,      \
                                       (ti)->DW_AT_bit_size,                  \
                                       (ti)->DW_AT_bit_offset)                \
                               :                                              \
                               (ti).typename),                                \
                              (p))



int Type_traverseTypeInfo(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn);


int Type_traverseBaseType(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    return fn(dict,data,type_info);
}

int Type_traverseEnum(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    return fn(dict,data,type_info);
}

int Type_traversePointer(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    return fn(dict,data,type_info);
}

int Type_traverseArray(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    TYPE_INFO subrange_info,element_info;
    ull *upper_bound,*byte_size;
    int status=fn(dict,data,type_info);

    if (Type_findBasic(dict,type_info->nexttype,&ZERO(element_info)) &&
        Type_getTypeInfo(Type_getChild(dict,type_info->item,NULL,0),&ZERO(subrange_info)))
    {
        upper_bound=STRTOULLP(subrange_info.DW_AT_upper_bound);
        byte_size=STRTOULLP(element_info.DW_AT_byte_size);
        if (upper_bound && *upper_bound && byte_size)
        {
            int i;
            for (i=0;i<=*upper_bound;i++)
            {
                char name[256];
                if (strlen(type_info->name))
                    sprintf(name,"%s.%d",type_info->name,i);
                else
                    sprintf(name,"%d",i);
                Type_combine(&element_info,type_info->addr+(i*(*byte_size)),name);
                Type_traverseTypeInfo(dict,data,&element_info,fn);
            }
        }
    }

    return status;
}

int Type_traverseTypedef(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    int status=fn(dict,data,type_info);

    if (Type_findBasic(dict,type_info->item,type_info))
        Type_traverseTypeInfo(dict,data,type_info,fn);

    return status;
}

int Type_traverseStruct(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    unsigned int i=0;
    DICT_ITEM *memberitem;
    TYPE_INFO local_type_info;
    int status=fn(dict,data,type_info);

    while (Type_getTypeInfo(Type_getChild(dict,type_info->item,NULL,i++),&ZERO(local_type_info)))
    {
        char *name=strlen(type_info->name)?
                          FORMATA(name,256,"%s.%s",type_info->name,local_type_info.DW_AT_name):
                          local_type_info.DW_AT_name;
        Type_combine(&local_type_info,type_info->addr,name);
        Type_traverseTypeInfo(dict,data,&local_type_info,fn);
    }

    return status;
}

int Type_traverseUnion(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    TYPE_INFO local_type_info;
    int status=fn(dict,data,type_info);

    if (Type_findBasic(dict,Type_getChild(dict,type_info->item,"dutype",0),&ZERO(local_type_info)) &&
        !strcmp(local_type_info.category,"enumeration_type"))
    {
        unsigned int value=*(unsigned int *) type_info->addr;
        if (Type_getTypeInfo(Type_getChild(dict,type_info->item,NULL,value),&ZERO(local_type_info)))
        {
            char *name=strlen(type_info->name)?
                              FORMATA(name,256,"%s.%s",type_info->name,local_type_info.DW_AT_name):
                              local_type_info.DW_AT_name;
            Type_combine(&local_type_info,type_info->addr,name);
            Type_traverseTypeInfo(dict,data,&local_type_info,fn);
        }
    }

    return status;
}

int Type_traverseMember(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    long long *loc=Type_getLocation(type_info->DW_AT_data_member_location);
    ull offset = loc?*loc:0;
    char *addr = type_info->addr;
    char *name = type_info->name;
    int index = type_info->index;
    int status=fn(dict,data,type_info);

    if (Type_findBasic(dict,type_info->item,type_info))
    {
        Type_combine(type_info,addr+offset,name);
        Type_traverseTypeInfo(dict,data,type_info,fn);
    }

    return status;
}

int Type_traverseVariable(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    long long *loc;
    //char *name=type_info->DW_AT_name;
    int status=fn(dict,data,type_info);

    if (loc=Type_getLocation(type_info->DW_AT_location))
    {
        char *newaddr = (char *) (unsigned long) *loc;
        Type_findBasic(dict,type_info->item,type_info);
        Type_combine(type_info,newaddr,type_info->name);
        Type_traverseTypeInfo(dict,data,type_info,fn);
    }

    return status;
}

int Type_traverseTypeInfo(DICT *dict,void *data,TYPE_INFO *type_info,Type_traverseFn fn)
{
    if (!strcmp(type_info->category,"base_type"))             Type_traverseBaseType(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"enumeration_type")) Type_traverseEnum(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"pointer_type"))     Type_traversePointer(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"array_type"))       Type_traverseArray(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"typedef"))          Type_traverseTypedef(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"structure_type"))   Type_traverseStruct(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"union_type"))       Type_traverseUnion(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"member"))           Type_traverseMember(dict,data,type_info,fn);
    else if (!strcmp(type_info->category,"variable"))         Type_traverseVariable(dict,data,type_info,fn);
}

int Type_traverse(DICT *dict,DICT_ITEM *typeitem,char *addr,void *data,Type_traverseFn fn)
{
    if (dict && typeitem)
    {
        TYPE_INFO type_info;
        Type_getTypeInfo(typeitem,&ZERO(type_info));
        Type_combine(&type_info,addr,"");
        Type_traverseTypeInfo(dict,data,&type_info,fn);
    }
}


char *Type_humanReadableVal(TYPE_INFO *type_info,char *buf)
{
    strcpy(buf,"n/a");

    if (!strcmp(type_info->category,"base_type"))
    {
        TYPE_UVALUE uval;
        char buf2[64];
        if (Type_getUVAL(type_info,type_info->addr,&uval))
            sprintf(buf,"%s",Type_pushUVAL(&uval,buf2));
    }
    else if (!strcmp(type_info->category,"enumeration_type"))
    {
        TYPE_UVALUE uval;
        if (Type_getUVAL(type_info,type_info->addr,&uval))
        {
            unsigned int value;
            char *valstr;
            DICT_ITEM *enumitem;

            UVAL2VAR(uval,value);
            valstr=ulltostr("values.%llu",value);
            enumitem=jli_getitem(&type_info->item->dict,valstr,strlen(valstr),0);

            if (enumitem) sprintf(buf,ENUMS_PREFIX "%s",enumitem->data);
            else sprintf(buf,"%lld",value);
        }
    }
    else if (!strcmp(type_info->category,"pointer_type"))
    {
        void *newaddr=(void *) *(unsigned int *) type_info->addr;
        sprintf(buf,"0x%x",newaddr);
    }

    return buf;
}




int Type_dumpTypeInfo(DICT *dict,void *data,TYPE_INFO *type_info)
{
    char buf[64];
    data=strlen((char *) data)?data:"%50s :%s\n";

    if (!strcmp(type_info->category,"base_type") ||
        !strcmp(type_info->category,"enumeration_type") ||
        !strcmp(type_info->category,"pointer_type"))
    {
        printf(data,Type_humanReadableVal(type_info,buf),type_info->name);
    }

    return 0;
}

void Type_dump(DICT *dict,DICT_ITEM *typeitem,char *addr,void *data)
{
    Type_traverse(dict,typeitem,addr,data,Type_dumpTypeInfo);
}




int Type_installTypeInfo(DICT *dict,void *data,TYPE_INFO *type_info)
{
    char buf[64];

    if (!strcmp(type_info->category,"base_type") ||
        !strcmp(type_info->category,"enumeration_type") ||
        !strcmp(type_info->category,"pointer_type"))
        jli_install(dict,Type_humanReadableVal(type_info,buf),type_info->name);

    return 0;
}

void Type_jvar(DICT *dict,DICT_ITEM *typeitem,char *addr,void *data)
{
    Type_traverse(dict,typeitem,addr,data,Type_installTypeInfo);
}



void Type_ref(DICT *dict,DICT_ITEM *typeitem,char *addr)
{
    if (typeitem && addr)
    {
        TYPE_INFO type_info;
        ZERO(type_info);
        Type_getTypeInfo(typeitem,&type_info);
        jli_install(dict,"","");
        jli_install(dict,ulltostr("0x%llx",(ull) addr),".addr");
        jli_install(dict,type_info.typename,".type");
    }
}



void Type_deref(DICT *dict,DICT_ITEM *typeitem,char *addr)
{
    if (typeitem && addr)
    {
        TYPE_INFO type_info;
        ZERO(type_info);
        if (Type_findBasic(dict,typeitem,&type_info) &&
            !strcmp(type_info.category,"pointer_type"))
            Type_ref(dict,type_info.nexttype,*((char **) (addr)));
    }
}

void Type_member(DICT *dict,DICT_ITEM *typeitem,char *addr,char *member)
{
    if (dict && typeitem && addr && member)
    {
        int dlen=hdict_delimit(member,strlen(member));
        TYPE_INFO type_info;
        DICT_ITEM *basicitem=Type_findBasic(dict,typeitem,&ZERO(type_info));
        Type_getTypeInfo(basicitem,&ZERO(type_info)); // re-retrieve so we don't mistake a member's name for struct's

        if (dlen)
        {
            ull *index,*byte_size;
            char *container_name;
            ull offset=0;

            if (!(container_name=type_info.DW_AT_name))
                container_name=type_info.type_id;

            if (!strcmp(type_info.category,"array_type") &&
                (index=STRTOULLP(member)) &&
                (typeitem=Type_getTypeInfo(type_info.nexttype,&type_info)) &&
                (byte_size=STRTOULLP(type_info.DW_AT_byte_size)))
            {
                offset=((*index)*(*byte_size));
            }
            else if ((typeitem=Type_getTypeInfo(Type_getChild(dict,basicitem,member,0),&ZERO(type_info))))
            {
                DICT_ITEM *basicitem=Type_findBasic(dict,typeitem,&ZERO(type_info));
                ull *loc=Type_getLocation(type_info.DW_AT_data_member_location);
                offset=loc?*loc:0;
            }

            if (member[dlen]=='.')
                return Type_member(dict,typeitem,addr+offset,member+dlen+1);
            else
                addr+=offset;
        }

        jli_install(dict,"","");
        jli_install(dict,ulltostr("0x%llx",(unsigned) addr),".addr");
        jli_install(dict,type_info.typename,".type");
    }
}

void Type_var(DICT *dict,DICT_ITEM *typeitem)
{
    if (typeitem)
    {
        TYPE_INFO type_info;
        ull *loc;
        ZERO(type_info);
        Type_getTypeInfo(typeitem,&type_info);
        if (loc=Type_getLocation(type_info.DW_AT_location))
            Type_ref(dict,typeitem,(char *) *loc);
    }
}

void Type_cgetbin(DICT *dict,DICT_ITEM *typeitem,char *addr)
{
    TYPE_INFO type_info;
    ull *byte_size;
    if ((typeitem=Type_findBasic(dict,typeitem,&ZERO(type_info))) &&
        (byte_size=STRTOULLP(type_info.DW_AT_byte_size)))
        bufpush(dict,addr,*byte_size);
}

void Type_csetbin(DICT *dict,DICT_ITEM *typeitem,char *addr,void *data,int len)
{
    TYPE_INFO type_info;
    ull *byte_size;
    if ((typeitem=Type_findBasic(dict,typeitem,&ZERO(type_info))) &&
        (byte_size=STRTOULLP(type_info.DW_AT_byte_size)))
        memcpy(addr,data,len<*byte_size?len:*byte_size);
}

void Type_cget(DICT *dict,DICT_ITEM *typeitem,char *addr)
{
    TYPE_INFO type_info;
    char buf[64];
    ull *byte_size;
    if ((typeitem=Type_findBasic(dict,typeitem,&ZERO(type_info))) &&
        (byte_size=STRTOULLP(type_info.DW_AT_byte_size)))
    {
        TYPE_UVALUE uval;
        Type_getUVAL(&type_info,addr,&uval);
        if (uval.dutype==TYPE_NONE)
            Type_ref(dict,typeitem,addr);
        else
            strpush(dict,Type_pushUVAL(&uval,buf));
    }
}

void Type_cset(DICT *dict,DICT_ITEM *typeitem,char *addr,void *data,int len)
{
    TYPE_INFO type_info;
    ull *byte_size;
    if ((typeitem=Type_findBasic(dict,typeitem,&ZERO(type_info))) &&
        (byte_size=STRTOULLP(type_info.DW_AT_byte_size)))
    {
        TYPE_UVALUE uval;
        Type_getUVAL(&type_info,addr,&uval);
        if (uval.dutype==TYPE_NONE)
            memcpy(addr,data,len<*byte_size?len:*byte_size);
        else
            Type_putUVAL(&type_info,addr,Type_pullUVAL(&uval,data));
    }
}



JLI_EXTENSION(reflect_var)
{
    void *arg=jli_pop(dict,"",0); // varname
    if (arg) Type_var(dict,Type_lookupName(dict,arg));
    DELETE(arg);
    return 0;
}

JLI_EXTENSION(reflect_dump)
{
    DICT_ITEM *sep=jli_getitem(dict,"",0,1);
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (sep && var)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc)
        {
            Type_dump(dict,Type_lookupName(dict,type),(char *) *loc,sep->data);
        }
    }
    DELETE(dict_free(var));
    DELETE(dict_free(sep));
    return 0;
}

JLI_EXTENSION(reflect_jvar)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc)
            Type_jvar(dict,Type_lookupName(dict,type),(char *) *loc,NULL);
    }
    DELETE(dict_free(var));
    return 0;
}

JLI_EXTENSION(reflect_deref)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc) Type_deref(dict,Type_lookupName(dict,type),(char *) *loc);
    }
    DELETE(dict_free(var));
    return 0;
}

JLI_EXTENSION(reflect_member)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc) Type_member(dict,Type_lookupName(dict,type),(char *) *loc,name);
    }
    DELETE(dict_free(var));
    return 0;
}

JLI_EXTENSION(reflect_cget)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc) Type_cget(dict,Type_lookupName(dict,type),(char *) *loc);
    }
    DELETE(dict_free(var));
    return 0;
}


JLI_EXTENSION(reflect_cset)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    DICT_ITEM *val=jli_getitem(dict,"",0,1);
    if (var && val)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc) Type_cset(dict,Type_lookupName(dict,type),(char *) *loc,val->data,val->datalen);
    }
    DELETE(dict_free(val));
    DELETE(dict_free(var));
    return 0;
}

JLI_EXTENSION(reflect_cgetbin)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc) Type_cgetbin(dict,Type_lookupName(dict,type),(char *) *loc);
    }
    DELETE(dict_free(var));
    return 0;
}


JLI_EXTENSION(reflect_csetbin)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    DICT_ITEM *val=jli_getitem(dict,"",0,1);
    if (var && val)
    {
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        if (type && loc) Type_csetbin(dict,Type_lookupName(dict,type),(char *) *loc,val->data,val->datalen);
    }
    DELETE(dict_free(val));
    DELETE(dict_free(var));
    return 0;
}

JLI_EXTENSION(reflect_cnew)
{
    char *arg=jli_pop(dict,"",0);
    TYPE_INFO type_info;
    DICT_ITEM *typeitem=Type_findBasic(dict,Type_lookupName(dict,arg),&ZERO(type_info));
    ull *byte_size;
    if (typeitem && (byte_size=STRTOULLP(type_info.DW_AT_byte_size)))
        Type_ref(dict,typeitem,mymalloc(*byte_size));
    DELETE(arg);
    return 0;
}

JLI_EXTENSION(reflect_cdel)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    ull *loc;
    if (var && (loc=INTVAR(&var->dict,"addr",0)))
        myfree((void *) *loc,0);
    DELETE(dict_free(var));
    return 0;
}

JLI_EXTENSION(reflect_czero)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        TYPE_INFO type_info;
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        DICT_ITEM *typeitem=Type_findBasic(dict,Type_lookupName(dict,type),&ZERO(type_info));
        ull *byte_size=STRTOULLP(type_info.DW_AT_byte_size);
        if (byte_size && type && loc) bzero((char *) *loc,*byte_size);
    }
    DELETE(dict_free(var));
    return 0;
}


#define CVAR_NAMESPACE "CVAR"
#define CVAR_NAMESPACE_STRLEN 4

JLI_EXTENSION(reflect_cvar)
{
    hdict_copy(dict,CVAR_NAMESPACE,CVAR_NAMESPACE_STRLEN);
    return reflect_member(dict,name,namelen,elevate);
}

JLI_EXTENSION(reflect_cvarget)
{
    hdict_copy(dict,CVAR_NAMESPACE,CVAR_NAMESPACE_STRLEN);
    reflect_member(dict,name,namelen,elevate);
    return reflect_cget(dict,name,namelen,elevate);
}

JLI_EXTENSION(reflect_cvarset)
{
    hdict_copy(dict,CVAR_NAMESPACE,CVAR_NAMESPACE_STRLEN);
    reflect_member(dict,name,namelen,elevate);
    return reflect_cset(dict,name,namelen,elevate);
}

JLI_EXTENSION(reflect_cvar_begin)
{
    hdict_name(dict,CVAR_NAMESPACE,CVAR_NAMESPACE_STRLEN);
    return 0;
}

JLI_EXTENSION(reflect_cvar_end)
{
    DELETE(dict_free(hdict_getitem(dict,CVAR_NAMESPACE,CVAR_NAMESPACE_STRLEN,1)));
    return 0;
}

JLI_EXTENSION(reflect_permute)
{
    Type_permute(dict,"types");
    return 0;
}


static char *reflection_fifoname=NULL;
static DICT *reflection_dict=NULL;

char *reflect_enumstr(char *type,unsigned int value)
{
    TYPE_INFO type_info;
    DICT_ITEM *enumitem,*typeitem;
    char *valstr=ulltostr("values.%llu",(ull)value);

    return ((typeitem=Type_findBasic(reflection_dict,Type_lookupName(reflection_dict,type),&type_info)) &&
            (enumitem=jli_getitem(&typeitem->dict,valstr,strlen(valstr),0)))?
        enumitem->data:NULL;
}

void reflect_vardump(char *type,void *addr,char *prefix)
{
    Type_dump(reflection_dict,Type_lookupName(reflection_dict,type),(char *) addr,prefix);
}

void reflect_pushvar(char *type,void *addr)
{
    Type_ref(reflection_dict,Type_lookupName(reflection_dict,type),(char *)addr);
}

void reflect_init(char *binname,char *fifoname)
{
    char *cmd;

    reflection_dict=jliext_init();

    jli_parse(reflection_dict,ulltostr("'0x%llx@var",      (ull) (void *) reflect_var));
    jli_parse(reflection_dict,ulltostr("'0x%llx@member",   (ull) (void *) reflect_member));
    jli_parse(reflection_dict,ulltostr("'0x%llx@dump",     (ull) (void *) reflect_dump));
    jli_parse(reflection_dict,ulltostr("'0x%llx@deref",    (ull) (void *) reflect_deref));
    jli_parse(reflection_dict,ulltostr("'0x%llx@cget",     (ull) (void *) reflect_cget));
    jli_parse(reflection_dict,ulltostr("'0x%llx@cset",     (ull) (void *) reflect_cset));
    jli_parse(reflection_dict,ulltostr("'0x%llx@cgetbin",  (ull) (void *) reflect_cgetbin));
    jli_parse(reflection_dict,ulltostr("'0x%llx@csetbin",  (ull) (void *) reflect_csetbin));
    jli_parse(reflection_dict,ulltostr("'0x%llx@cnew",     (ull) (void *) reflect_cnew));
    jli_parse(reflection_dict,ulltostr("'0x%llx@cdel",     (ull) (void *) reflect_cdel));
    jli_parse(reflection_dict,ulltostr("'0x%llx@czero",    (ull) (void *) reflect_czero));
    jli_parse(reflection_dict,ulltostr("'0x%llx@jvar",     (ull) (void *) reflect_jvar));
    jli_parse(reflection_dict,ulltostr("'0x%llx@permute",  (ull) (void *) reflect_permute));
    DICT_BYTECODE(reflection_dict,"<",reflect_cvar_begin);
    DICT_BYTECODE(reflection_dict,">",reflect_cvar_end);
    DICT_BYTECODE(reflection_dict,"`",reflect_cvar);
    DICT_BYTECODE(reflection_dict,";",reflect_cvarget);
    DICT_BYTECODE(reflection_dict,":",reflect_cvarset);

    jli_frame_begin(reflection_dict);

    reflection_fifoname = fifoname;
    mkfifo(reflection_fifoname,777);

    printf("Ready!\n");
}

#endif
