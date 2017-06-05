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
#include <dlfcn.h> // dlopen/dlsym/dlclose

#include <ffi.h>

#include "util.h"
#include "listree.h"
#include "reflect.h"

char *Type_pushUVAL(TYPE_UVALUE *uval,char *buf);
TYPE_UVALUE *Type_pullUVAL(TYPE_UVALUE *uval,char *buf);
TYPE_UTYPE Type_getUVAL(LTV *cvar,TYPE_UVALUE *uval);
int Type_putUVAL(LTV *cvar,TYPE_UVALUE *uval);


/////////////////////////////////////////////////////////////

LTV *attr_set(LTV *ltv,char *attr,char *val) { return LT_put(ltv,attr,TAIL,LTV_new(val,-1,LT_DUP)); }
LTV *attr_own(LTV *ltv,char *attr,char *val) { return LT_put(ltv,attr,TAIL,LTV_new(val,-1,LT_OWN)); }
LTV *attr_imm(LTV *ltv,char *attr,long long imm)    { return LT_put(ltv,attr,TAIL,LTV_new((void *) imm,0,LT_IMM)); }

char *attr_get(LTV *ltv,char *attr)
{
    LTV *attr_ltv=LT_get(ltv,attr,TAIL,KEEP);
    return attr_ltv?attr_ltv->data:NULL;
}

void attr_del(LTV *ltv,char *attr)
{
    LTI *lti=LTI_resolve(ltv,attr,false);
    if (lti)
        RBN_release(&ltv->sub.ltis,&lti->rbn,LTI_release);
}

// look up an attribute in one LTV, and look up it's value within another LTV.
char *attr_deref(LTV *ltv,char *attr,LTV *index)
{
    char *attr_val=attr_get(ltv,attr);
    if (attr_val)
        return attr_get(index,attr_val);
}

/////////////////////////////////////////////////////////////
//
// Dwarf Traversal
//
/////////////////////////////////////////////////////////////

typedef int (*DIE_OP)(Dwarf_Debug dbg,Dwarf_Die die);

int traverse_die(Dwarf_Debug dbg,Dwarf_Die die,DIE_OP op)
{
    int status=0;
    if (die) {
        STRY(op(dbg,die),"operating on die");
        dwarf_dealloc(dbg,die,DW_DLA_DIE);
    }
 done:
    return status;
}

int traverse_child(Dwarf_Debug dbg,Dwarf_Die die,DIE_OP op)
{
    int status=0;
    Dwarf_Error error;
    Dwarf_Die child=0;
    TRY(dwarf_child(die,&child,&error),"retrieving dwarf_child");
    CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking dwarf child's existence");
    SCATCH("checking dwarf_child for error");
    STRY(traverse_die(dbg,child,op),"processing child");
 done:
    return status;
}

int traverse_sibling(Dwarf_Debug dbg,Dwarf_Die die,DIE_OP op)
{
    int status=0;
    Dwarf_Error error;
    Dwarf_Die sibling=0;
    TRY(dwarf_siblingof(dbg,die,&sibling,&error),"retrieving dwarf_sibling");
    CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for DW_DLV_NO_ENTRY"); /* Done at this level. */
    SCATCH("checking dwarf_siblingof");
    STRY(traverse_die(dbg,sibling,op),"processing sibling");
 done:
    return status;
}

int traverse_cus(char *filename,DIE_OP op,CU_DATA *cu_data)
{
    int status=0;
    Dwarf_Debug dbg;
    Dwarf_Error error;

    int read_cu_list() {
        CU_DATA cu_data_local;
        if (!cu_data) cu_data=&cu_data_local; // allow caller to not care

        while (1) {
            TRY(dwarf_next_cu_header_b(dbg,
                                       &cu_data->header_length,
                                       &cu_data->version_stamp,
                                       &cu_data->abbrev_offset,
                                       &cu_data->address_size,
                                       &cu_data->length_size,
                                       &cu_data->extension_size,
                                       &cu_data->next_cu_header_offset,
                                       &error),
                "reading next cu header");
            DWARF_ID(cu_data->next_cu_header_offset_str,cu_data->next_cu_header_offset);
            CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for no next cu header");
            CATCH(status==DW_DLV_ERROR,status,goto done,"checking error dwarf_next_cu_header");
            STRY(traverse_sibling(dbg,NULL,op),"processing cu die and sibs");
        }
    done:
        return status;
    }

    int filedesc = -1;
    STRY((filedesc=open(filename,O_RDONLY))<0,"opening dwarf2edict input file %s",filename);
    TRYCATCH(dwarf_init(filedesc,DW_DLC_READ,NULL,NULL,&dbg,&error),status,close_file,"initializing dwarf reader");
    TRYCATCH(read_cu_list(),status,close_dwarf,"reading cu list");
 close_dwarf:
    STRY(dwarf_finish(dbg,&error),"finalizing dwarf reader");
 close_file:
    close(filedesc);
 done:
    return status;
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

int print_cu_data(FILE *ofile,CU_DATA *cu_data)
{
    int status=0;
    STRY(!cu_data,"validating cu data");
    fprintf(ofile,"CU DATA");
#define output_cu_field(fmt,field) fprintf(ofile,"|" #field fmt,cu_data->field)
    output_cu_field("%s",offset_str);
    output_cu_field("%s",next_cu_header_offset_str);
    output_cu_field("%x",header_length);
    output_cu_field("%x",version_stamp);
    output_cu_field("%x",abbrev_offset);
    output_cu_field("%x",address_size);
    output_cu_field("%x",length_size);
    output_cu_field("%x",extension_size);
 done:
    return status;
}

int dot_cu_data(FILE *ofile,CU_DATA *cu_data)
{
    int status=0;
    STRY(!cu_data,"validating cu data");
    fprintf(ofile,CVAR_FORMAT " [shape=record label=\"{");
    print_cu_data(ofile,cu_data);
    fprintf(ofile,"}\"");
    fprintf(ofile,"]\n");
 done:
    return status;
}


int print_type_info(FILE *ofile,TYPE_INFO *type_info)
{
    int status=0;
    fprintf(ofile,"TYPE_INFO %s",type_info->id_str);
    const char *str=NULL;
    dwarf_get_TAG_name(type_info->tag,&str);
    fprintf(ofile,"|%s",str+7);
    if (type_info->flags&TYPEF_BASE)       fprintf(ofile,"|base %s",        type_info->base_str);
    if (type_info->flags&TYPEF_CONSTVAL)   fprintf(ofile,"|constval %u",    type_info->const_value);
    if (type_info->flags&TYPEF_BYTESIZE)   fprintf(ofile,"|bytesize %u",    type_info->bytesize);
    if (type_info->flags&TYPEF_BITSIZE)    fprintf(ofile,"|bitsize %u",     type_info->bitsize);
    if (type_info->flags&TYPEF_BITOFFSET)  fprintf(ofile,"|bitoffset %u",   type_info->bitoffset);
    if (type_info->flags&TYPEF_ENCODING)   fprintf(ofile,"|encoding %u",    type_info->encoding);
    if (type_info->flags&TYPEF_UPPERBOUND) fprintf(ofile,"|upperbound 0x%x",type_info->upper_bound);
    if (type_info->flags&TYPEF_LOWPC)      fprintf(ofile,"|lowpc 0x%x",     type_info->low_pc);
    if (type_info->flags&TYPEF_MEMBERLOC)  fprintf(ofile,"|member loc 0x%x",type_info->data_member_location);
    if (type_info->flags&TYPEF_LOCATION)   fprintf(ofile,"|location 0x%x",  type_info->location);
    if (type_info->flags&TYPEF_ADDR)       fprintf(ofile,"|addr 0x%x",      type_info->addr);
    if (type_info->flags&TYPEF_EXTERNAL)   fprintf(ofile,"|external %u",    type_info->external);
    if (type_info->flags&TYPEF_SYMBOLIC)   fprintf(ofile,"|symbolic");
    if (type_info->dladdr)                 fprintf(ofile,"|dladdr 0x%x",    type_info->dladdr);
    if (type_info->flags&TYPEF_DQ)         fprintf(ofile,"|dq");
    return status;
}

int dot_type_info(FILE *ofile,TYPE_INFO *type_info)
{
    int status=0;
    fprintf(ofile,CVAR_FORMAT " [shape=record label=\"{",type_info);
    print_type_info(ofile,type_info);
    fprintf(ofile,"}\"");
    if (type_info->flags&TYPEF_DQ)
        fprintf(ofile," color=orange");
    else if (type_info->flags&TYPEF_UPPERBOUND)
        fprintf(ofile," color=white");
    switch (type_info->tag) {
        case DW_TAG_compile_unit:     fprintf(ofile," style=filled fillcolor=red rank=max"); break;
        case DW_TAG_subprogram:       fprintf(ofile," style=filled fillcolor=orange"); break;
        case DW_TAG_formal_parameter: fprintf(ofile," style=filled fillcolor=gold"); break;
        case DW_TAG_variable:         fprintf(ofile," style=filled fillcolor=cyan"); break;
        case DW_TAG_typedef:          fprintf(ofile," style=filled fillcolor=green"); break;
        case DW_TAG_structure_type:   fprintf(ofile," style=filled fillcolor=blue"); break;
        case DW_TAG_union_type:       fprintf(ofile," style=filled fillcolor=violet"); break;
        case DW_TAG_member:           fprintf(ofile," style=filled fillcolor=lightblue"); break;
        case DW_TAG_enumeration_type: fprintf(ofile," style=filled fillcolor=magenta"); break;
        case DW_TAG_array_type:       fprintf(ofile," style=filled fillcolor=gray80"); break;
        case DW_TAG_pointer_type:     fprintf(ofile," style=filled fillcolor=gray90"); break;
        case DW_TAG_base_type:        fprintf(ofile," style=filled fillcolor=yellow rank=min"); break;
        case DW_TAG_enumerator:       fprintf(ofile," style=filled fillcolor=pink rank=min"); break;
        case DW_TAG_subrange_type:    fprintf(ofile," style=filled fillcolor=gray80 rank=min"); break;
    }
    fprintf(ofile,"]\n");
 done:
    return status;
}


LTV *ref_find_basic(LTV *type)
{
    TYPE_INFO *type_info=NULL;
    do {
        type_info=(TYPE_INFO *) type->data;
        if (type_info->flags&TYPEF_BYTESIZE) // "basic" means a type that specifies memory size
            return type;
    } while (type=LT_get(type,TYPE_BASE,HEAD,KEEP));
    return NULL;
}

LTV *ref_get_child(LTV *type,char *member)
{
    if (type->flags&LT_TYPE) {
        LTV *subtype=LT_get(type,member,HEAD,KEEP);
        if (subtype->flags&LT_TYPE)
            return subtype;
    }
    return NULL;
}

LTV *ref_get_element(LTV *type,int index)
{
    // see Type_getChild/Type_findMemberByIndex
}

LTV *ref_create_cvar(LTV *type,void *data,char *member)
{
    int status=0;
    LTV *basic_type=NULL,*member_type=NULL,*cvar=NULL;
    TRYCATCH(!(basic_type=ref_find_basic(type)),0,done,"resolving basic type");
    TYPE_INFO *type_info=(TYPE_INFO *) basic_type->data;

    switch(type_info->tag) {
        case DW_TAG_structure_type:
        case DW_TAG_union_type:
            if (member) {
                STRY(!(member_type=ref_get_child(basic_type,member)),"retrieving cvar member");
                TYPE_INFO *member_type_info=(TYPE_INFO *) member_type->data;
                return ref_create_cvar(member_type,data+member_type_info->data_member_location,NULL);
            }
            break;
        default:
            break;
    }

    int size=type_info->bytesize;
    if (data)
        STRY(!(cvar=LTV_new(data,size,LT_BIN|LT_CVAR)),"creating reference cvar");
    else
        STRY(!(cvar=LTV_new((void *) mymalloc(size),size,LT_OWN|LT_BIN|LT_CVAR)),"creating allocated cvar");
    LT_put(cvar,CVAR_TYPE,HEAD,type);
 done:
    return status?NULL:cvar;
}

LTV *ref_assign_cvar(LTV *cvar,LTV *ltv)
{
    int status=0;
    LTV *type=NULL;
    TYPE_UVALUE uval;
    Type_getUVAL(cvar,&uval);
    if (uval.base.dutype)
        STRY(Type_putUVAL(cvar,Type_pullUVAL(&uval,ltv->data)),"converting text value to cvar");
 done:
    return status?NULL:cvar;
}

void *cvar_map(LTV *ltv,void *(*op)(LTV *cvar,LT_TRAVERSE_FLAGS *flags))
{
    void *traverse_types(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        if ((*flags==LT_TRAVERSE_LTV) && (*ltv)->flags&LT_CVAR)
            return op((*ltv),flags);
        return NULL;
    }
    return ltv_traverse(ltv,traverse_types,NULL);
}

int ref_dump_cvar(FILE *ofile,LTV *cvar,int maxdepth)
{
    int status=0;
    LTV *type;
    STRY(!(type=LT_get(cvar,CVAR_TYPE,HEAD,KEEP)),"validating cvar via type");
    CLL queue;
    CLL_init(&queue);
    LTV_enq(&queue,ref_create_cvar(type,cvar->data,NULL),TAIL); // copy cvar so we don't mess with it

    int process_type_info(LTV *cvar) {
        int status=0;
        LTV *type=NULL;

        void *type_info_op(LTV *ltv,LT_TRAVERSE_FLAGS *flags) {
            int status=0;
            TRYCATCH(!(ltv->flags&LT_TYPE) || ltv==type,0,done,"skipping non-TYPE_INFO and parent-type ltvs");
            *flags|=LT_TRAVERSE_HALT;
            TYPE_INFO *type_info=(TYPE_INFO *) ltv->data;
            switch (type_info->tag) {
                case DW_TAG_member:
                    LTV_enq(&queue,ref_create_cvar(ltv,cvar->data+type_info->data_member_location,NULL),TAIL);
                    break;
                default:
                    fprintf(ofile,"    child tag %d unimplemented\n",type_info->tag);
                    break;
            }
        done:
            return status?NON_NULL:NULL;
        }

        STRY(!(type=LT_get(cvar,CVAR_TYPE,HEAD,KEEP)),"looking up cvar type");
        TYPE_INFO *type_info=(TYPE_INFO *) type->data;

        char *name=attr_get(type,TYPE_NAME);
        const char *str=NULL;
        dwarf_get_TAG_name(type_info->tag,&str);
        fprintf(ofile," %s \"%s\",",str+7,name);

        switch(type_info->tag) {
            case DW_TAG_union_type:
            case DW_TAG_structure_type:
                cvar_map(type,type_info_op); // traverse type hierarchy
                break;
            case DW_TAG_pointer_type: {
                LTV *base_type=LT_get(type,TYPE_BASE,HEAD,KEEP);
                if (base_type)
                    LTV_enq(&queue,ref_create_cvar(base_type,*(void **) cvar->data,NULL),HEAD);
                break;
            }
            case DW_TAG_array_type:
                fprintf(ofile," (array unimplemented)");
                break;
            case DW_TAG_enumeration_type:
                fprintf(ofile," (enum unimplemented)");
                break;
            case DW_TAG_enumerator:
                fprintf(ofile,"enumerator!!!\n");
                break;
            case DW_TAG_member:
                if (type_info->flags&TYPEF_BYTESIZE) {
                    print_type_info(ofile,type_info);
                    // and fall thru case!!!
                } else {
                    LTV_enq(&queue,ref_create_cvar(ref_find_basic(type),cvar->data,NULL),HEAD);
                    break;
                }
            case DW_TAG_base_type: {
                TYPE_UVALUE uval;
                char buf[64];
                if (Type_getUVAL(cvar,&uval))
                    fprintf(ofile," %s",Type_pushUVAL(&uval,buf));
                break;
            }
            default:
                LTV_enq(&queue,ref_create_cvar(ref_find_basic(type),cvar->data,NULL),HEAD);
                break;
        }
    done:
        fprintf(ofile,"\n");
        return status;
    }

    while ((cvar=LTV_deq(&queue,HEAD))) {
        process_type_info(cvar);
        LTV_release(cvar);
    }

 done:
    return status;
}


int ref_print_cvar(FILE *ofile,LTV *ltv)
{
    int status=0;
    if (ltv->flags&LT_TYPE) // special case
        print_type_info(ofile,(TYPE_INFO *) ltv->data);
    else
        ref_dump_cvar(ofile,ltv,0); // use reflection!!!!!
 done:
    return status;
}

int ref_dot_cvar(FILE *ofile,LTV *ltv)
{
    int status=0;
    if (ltv->flags&LT_TYPE)
        dot_type_info(ofile,(TYPE_INFO *) ltv->data);
    fprintf(ofile,"\"LTV%x\" -> " CVAR_FORMAT " [color=purple]\n",ltv,ltv->data); // link ltv to type_info
 done:
    return status;
}


void graph_types_to_file(char *filename,LTV *ltv) {
    FILE *ofile=fopen(filename,"w");
    CLL ltvs;
    CLL_init(&ltvs);
    LTV_enq(&ltvs,ltv,HEAD);
    fprintf(ofile,"digraph iftree\n{\ngraph [rankdir=LR /*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");
    ltvs2dot_simple(ofile,&ltvs,0,filename);
    //ltvs2dot(ofile,&ltvs,0,filename);
    fprintf(ofile,"}\n");
    LTV_deq(&ltvs,HEAD);
    fclose(ofile);
}


#define IF_OK(cond,followup) if (cond==DW_DLV_OK) followup

int populate_type_info(Dwarf_Debug dbg,Dwarf_Die die,LTV *type_info_ltv,CU_DATA *cu_data)
{
    int status=0;
    Dwarf_Error error;

    char *diename = NULL;
    Dwarf_Off global_offset;

    STRY(!type_info_ltv || !cu_data,"validating params");
    TYPE_INFO *type_info=(TYPE_INFO *) type_info_ltv->data;

    STRY(die==NULL,"testing for null die");
    STRY(dwarf_dieoffset(die,&global_offset,&error),"getting global die offset");
    DWARF_ID(type_info->id_str,global_offset);
    DWARF_ID(cu_data->offset_str,global_offset);
    STRY(dwarf_tag(die,&type_info->tag,&error),"getting die tag");

    switch (type_info->tag)
    {
        case DW_TAG_pointer_type:
        case DW_TAG_array_type:
        case DW_TAG_volatile_type:
        case DW_TAG_const_type:
        case DW_TAG_structure_type:
        case DW_TAG_union_type:
        case DW_TAG_enumeration_type:
        case DW_TAG_compile_unit:
        case DW_TAG_base_type:
        case DW_TAG_typedef:
        case DW_TAG_subrange_type:
        case DW_TAG_member:
        case DW_TAG_enumerator:
        case DW_TAG_subprogram:
        case DW_TAG_subroutine_type:
        case DW_TAG_formal_parameter:
        case DW_TAG_variable:
        case DW_TAG_unspecified_parameters: // varargs
            break;
        default:
            printf(CODE_RED "Unrecognized tag 0x%x\n" CODE_RESET,type_info->tag);
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
            //type_info->tag=0; // reject
            goto done;
    }


    Dwarf_Signed atcnt=0;
    Dwarf_Attribute *atlist=NULL;
    TRY(dwarf_attrlist(die,&atlist,&atcnt,&error),"getting die attrlist");
    CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for DW_DLV_NO_ENTRY in dwarf_attrlist");
    SCATCH("getting die attrlist");

    Dwarf_Attribute *attr=NULL;
    while (atcnt--) {
        char *prefix;
        attr=&atlist[atcnt];

        Dwarf_Half vshort;
        STRY(dwarf_whatattr(*attr,&vshort,&error),"getting attr type");

        switch (vshort) {
            case DW_AT_name: // string
                break;
            case DW_AT_type: // global_formref
                IF_OK(dwarf_global_formref(*attr,&global_offset,&error),type_info->flags|=TYPEF_BASE);
                DWARF_ID(type_info->base_str,global_offset);
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
                IF_OK(dwarf_formudata(*attr,&type_info->upper_bound,&error),type_info->flags|=TYPEF_UPPERBOUND);
                break;
            case DW_AT_encoding: // DW_ATE_unsigned, etc.
                IF_OK(dwarf_formudata(*attr,&type_info->encoding,&error),type_info->flags|=TYPEF_ENCODING);
                break;
            case DW_AT_GNU_vector: // "The main difference between a regular array and the vector variant is that vectors are passed by value to functions."
                type_info->flags|=TYPEF_VECTOR; // an attribute of an array
                break;
            case DW_AT_sibling:
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
          Dwarf_Signed vint;
          Dwarf_Unsigned vuint;
          Dwarf_Addr vaddr;
          Dwarf_Off voffset;
          Dwarf_Half vshort;
          Dwarf_Bool vbool;
          Dwarf_Ptr vptr;
          Dwarf_Block *vblock;
          Dwarf_Sig8 vsig8;
          char *vstr;
          const char *vcstr;

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

        Dwarf_Unsigned vuint;
        Dwarf_Ptr vptr;
        IF_OK(dwarf_formexprloc(*attr,&vuint,&vptr,&error),get_expr_loclist_data(vuint,vptr));

        dwarf_dealloc(dbg,atlist[atcnt],DW_DLA_ATTR);
    }
    dwarf_dealloc(dbg,atlist,DW_DLA_LIST);

 done:
    return status;
}

char *get_diename(Dwarf_Debug dbg,Dwarf_Die die)
{
    int status=0;
    Dwarf_Error error;
    char *diename=NULL;
    STRY(dwarf_diename(die,&diename,&error)==DW_DLV_ERROR,"checking dwarf_diename");
    char *type_info_name=diename?bufdup(diename,-1):NULL;
    dwarf_dealloc(dbg,diename,DW_DLA_STRING);
 done:
    return (status || !type_info_name)?NULL:type_info_name;
}


int ref_preview_module(LTV *module) // just put the cu name under module
{
    CU_DATA cu_data;
    int op(Dwarf_Debug dbg,Dwarf_Die die) {
        int status=0;
        char *cu_name=NULL;
        STRY(!(cu_name=get_diename(dbg,die)),"looking up cu die name");
        printf("%s\n",cu_name);
        STRY(!LT_put(module,"compile units",TAIL,LTV_new(cu_name,-1,LT_OWN)),"adding cu name to list of compute units");
    done:
        return status;
    }
    char *filename=PRINTA(filename,module->len,module->data);
    return traverse_cus(filename,op,&cu_data);
}

int resolve_symbols(LTV *module,char *dlname,LTV *index)
{
    int status=0;
    LTV *types=LTV_VOID,*global_types=LTV_VOID,*globals=LTV_VOID;
    void *dlhandle=NULL;

    int derive_symbolic_name(LTV *ltv)
    {
        int status=0;
        TYPE_INFO *type_info=(TYPE_INFO *) ltv->data;
        TRYCATCH(type_info->flags&TYPEF_SYMBOLIC,0,done,"checking if symbolic name already derived");
        LTV *base_ltv=NULL;
        TYPE_INFO *base_info=NULL;
        if (type_info->flags&TYPEF_BASE) { // link to base type
            STRY(!(base_ltv=LT_get(index,type_info->base_str,HEAD,KEEP)),"looking up base die for %s",type_info->id_str);
            base_info=(TYPE_INFO *) base_ltv->data;
        }

        char *type_name=attr_get(ltv,TYPE_NAME);
        char *base_symb=base_info && (base_info->flags&TYPEF_SYMBOLIC)? attr_get(base_ltv,TYPE_NAME):NULL;
        char *composite_name=NULL;

        void categorize_symbolic(LTV *category,char *sym) {
            type_info->flags|=TYPEF_SYMBOLIC;
            attr_del(ltv,TYPE_NAME);
            attr_set(ltv,TYPE_NAME,sym);
            if (category && !LT_get(category,sym,HEAD,KEEP)) { // if not a dup, place item into category
                LT_put(category,sym,TAIL,ltv);
                if (category==global_types) { // dynamically link globals
                    if (!type_info->dladdr) {
                        type_info->dladdr=dlsym(dlhandle,sym);
                        LT_put(globals,sym,HEAD,ref_create_cvar(ltv,type_info->dladdr,NULL));
                    }
                } else if (base_symb) { // dedup types (not global types) to get here, base must already be installed in "types"
                    LTV *symb_base=LT_get(types,base_symb,HEAD,KEEP);
                    if (symb_base && symb_base!=base_ltv) { // may already be correct
                        TYPE_INFO *new_base=(TYPE_INFO *) symb_base->data;
                        attr_del(ltv,TYPE_BASE);
                        LT_put(ltv,TYPE_BASE,TAIL,symb_base);
                        strncpy(type_info->base_str,new_base->id_str,TYPE_IDLEN);
                    }
                }
            }
        }

        switch(type_info->tag) {
            case DW_TAG_structure_type:
                if (type_name)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(type_name),"struct %s",type_name));
                break;
            case DW_TAG_union_type:
                if (type_name)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(type_name),"union %s",type_name));
                break;
            case DW_TAG_enumeration_type:
                if (type_name)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(type_name),"enum %s",type_name));
                break;
            case DW_TAG_pointer_type:
                if (!(type_info->flags&TYPEF_BASE))
                    base_symb="void";
                if (base_symb)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(base_symb),"(%s)*",base_symb));
                break;
            case DW_TAG_array_type:
                if (base_symb) {
                    LTV *subrange_ltv=LT_get(ltv,"subrange type",HEAD,KEEP);
                    TYPE_INFO *subrange=subrange_ltv?(TYPE_INFO *) subrange_ltv->data:NULL;
                    if (subrange && subrange->flags&TYPEF_UPPERBOUND) {
                        if (base_info && (base_info->flags&TYPEF_BYTESIZE)) {
                            type_info->bytesize=base_info->bytesize * (subrange->upper_bound+1);
                            type_info->flags|=TYPEF_BYTESIZE;
                        }
                        categorize_symbolic(types,FORMATA(composite_name,strlen(base_symb)+20,"(%s)[%d]",base_symb,subrange->upper_bound+1));
                    }
                    else
                        categorize_symbolic(types,FORMATA(composite_name,strlen(base_symb),"(%s)[]",base_symb));
                }
                break;
            case DW_TAG_volatile_type:
                if (base_symb)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(base_symb),"volatile %s",base_symb));
                break;
            case DW_TAG_const_type:
                if (base_symb)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(base_symb),"const %s",base_symb));
                break;
            case DW_TAG_base_type:
            case DW_TAG_enumerator:
                if (type_name)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(type_name),"%s",type_name));
                break;
            case DW_TAG_typedef:
                if (type_name)
                    categorize_symbolic(types,FORMATA(composite_name,strlen(type_name),"%s",type_name));
                else if (base_symb) // anonymous typedef
                    categorize_symbolic(types,FORMATA(composite_name,strlen(base_symb),"%s",base_symb));
                break;
            case DW_TAG_subprogram:
            case DW_TAG_subroutine_type:
                if (type_name) // GLOBAL!
                    categorize_symbolic(global_types,FORMATA(composite_name,strlen(type_name),"%s",type_name));
                break;
            case DW_TAG_variable:
                if (type_name) // GLOBAL!
                    categorize_symbolic(global_types,FORMATA(composite_name,strlen(type_name),"%s",type_name));
                break;
            case DW_TAG_compile_unit:
            case DW_TAG_subrange_type:
            case DW_TAG_member:
            case DW_TAG_formal_parameter:
            case DW_TAG_unspecified_parameters: // varargs
                if (type_name) // still want to dedup!
                    categorize_symbolic(NULL,FORMATA(composite_name,strlen(type_name),"%s",type_name));
                break;
            default: // no name
                break;
        }
    done:
        return status;
    }

    void *resolve_types(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        if ((*flags==LT_TRAVERSE_LTV) && (*ltv)->flags&LT_TYPE) { // finesse: LT_TRAVERSE_LTV won't match if listree_acyclic set LT_TRAVERSE_HALT
            (*lti)=LTI_resolve((*ltv),TYPE_BASE,false); // just descend types
            derive_symbolic_name(*ltv);
        }
        return NULL;
    }

    STRY(!(dlhandle=dlopen(dlname,RTLD_LAZY)),"opening module for dynamic linking");
    STRY(!LT_put(module,MOD_HDL,HEAD,LTV_new(dlhandle,0,LT_BIN)),"stashing module handle");
    STRY(ltv_traverse(index,resolve_types,resolve_types)!=NULL,"linking symbolic names"); // links symbols on pre- and post-passes
    LT_put(module,"type",TAIL,types);
    LT_put(module,"global_type",TAIL,global_types);
    LT_put(module,"global",TAIL,globals);

    //graph_types_to_file("/tmp/types.dot",types);

 done:
    return status;
}



// FIXME: USE dwarf_offdie_b TO DO ON-DEMAND TYPE RESOLUTION
/* dwarf_offdie_b() new October 2011 */
/*  Finding die given global (not CU-relative) offset.
    Applies to debug_info (is_info true) or debug_types (is_info false). */
int dwarf_offdie_b(Dwarf_Debug /*dbg*/,
                       Dwarf_Off        /*offset*/,
                       Dwarf_Bool       /*is_info*/,
                       Dwarf_Die*       /*return_die*/,
                       Dwarf_Error*     /*error*/);


int traverse_types(char *filename,LTV *module)
{
    int status=0;
    FILE *ofile=fopen(filename,"w");

    void *traverse_types(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags); // finesse: traverse flags below won't match if listree_acyclic sets LT_TRAVERSE_HALT
        if ((*flags==LT_TRAVERSE_LTV) && (*ltv)->flags&LT_CVAR && !((*ltv)->flags&LT_TYPE)) {
            TYPE_INFO *type_info=(TYPE_INFO *) (*ltv)->data;
            fprintf(ofile,"\"%s\" [label=\"%s\"]\n",type_info->id_str,attr_get((*ltv),TYPE_NAME));
            if (type_info->flags&TYPEF_BASE)
                fprintf(ofile,"\"%s\" -> \"%s\"\n",type_info->id_str,type_info->base_str);
        }
        return NULL;
    }

    // simple dump of just typenames linked to their base types
    fprintf(ofile,"digraph iftree\n{\ngraph [ratio=compress, concentrate=true] node [shape=record] edge []\n");
    STRY(ltv_traverse(LT_get(module,"type",HEAD,KEEP),traverse_types,NULL)!=NULL,"traversing module");
    fprintf(ofile,"}\n");
 done:
    fclose(ofile);
    return status;
}


int ref_curate_module(LTV *module,char *altname)
{
    int status=0;
    CU_DATA cu_data;

    LTV *compile_units=LTV_VOID;
    LTV *index=LTV_VOID;
    LTV *dependencies=LTV_VOID;
    int pass=0;

    int init(Dwarf_Debug dbg,Dwarf_Die die) {
        int work_op(LTV *parent,Dwarf_Die die) { // propagates parentage through the stateless DIE_OP calls
            int status=0;
            LTV *type_info_ltv=NULL;
            TYPE_INFO *type_info=NULL;

            int sib_op(Dwarf_Debug dbg,Dwarf_Die die)   { return work_op(parent,die); }
            int child_op(Dwarf_Debug dbg,Dwarf_Die die) { return work_op(type_info_ltv,die); }

            int disqualify() {
                switch (type_info->tag) {
                    case 0: // populate_type_info rejected it
                    case DW_TAG_variable:
                    case DW_TAG_subprogram:
                        if (!(type_info->flags&TYPEF_EXTERNAL))
                            return true;
                    default:
                        break;
                }
                return false;
            }

            int link2parent(char *name) {
                int status=0;
                switch(type_info->tag) {
                    case DW_TAG_compile_unit:
                        if (!LTV_empty(type_info_ltv) && name)
                            STRY(!LT_put(compile_units,name,TAIL,type_info_ltv),"linking cu to module");
                        break;
                    case DW_TAG_subprogram:
                    case DW_TAG_variable:
                        if (parent && name)
                            STRY(!LT_put(parent,name,TAIL,type_info_ltv),"linking extern subprogram/variable to parent");
                        break;
                    case DW_TAG_unspecified_parameters: // varargs
                        if (parent)
                            STRY(!LT_put(parent,"unspecified parameters",TAIL,type_info_ltv),"linking unspecified parameters to parent");
                        break;
                    case DW_TAG_subrange_type:
                        if (parent)
                            STRY(!LT_put(parent,"subrange type",TAIL,type_info_ltv),"linking subrange type to parent");
                        break;
                    default:
                        if (parent && name)
                            STRY(!LT_put(parent,name,TAIL,type_info_ltv),"linking type info to parent");
                        break;
                }
            done:
                return status;
            }

            int descend() {
                int status=0;
                char *name=get_diename(dbg,die); // name is allocated from heap...
                if (name)
                    STRY(!attr_own(type_info_ltv,TYPE_NAME,name),"naming type info");
                STRY(!LT_put(index,type_info->id_str,TAIL,type_info_ltv),"indexing type info");
                if (type_info->tag==DW_TAG_compile_unit) {
                    // Incremental load: Check to see if this CU needs to be loaded
                    // A) automatically, B) by request, or C) contains an unresolved die's base
                    int contains_dependent() {
                        LTI *lti=NULL;
                        LTV *ltv=NULL;
                        TYPE_INFO *base_info;
                        if ((lti=LTI_first(dependencies)) && (ltv=LTV_peek(&lti->ltvs,HEAD))) {
                            base_info=(TYPE_INFO *) ltv->data;
                            if (strncmp(base_info->base_str,cu_data.offset_str,TYPE_IDLEN)>0 &&
                                strncmp(base_info->base_str,cu_data.next_cu_header_offset_str,TYPE_IDLEN)<0)
                                return true;
                        }
                        return false;
                    }
                    if (pass==0 || contains_dependent())
                        STRY(traverse_child(dbg,die,child_op),"traversing child");
                } else { // attach dies to base by id to resolve dependency graph; later, dedup by relinking bases symbolically
                    STRY(traverse_child(dbg,die,child_op),"traversing child");
                    if (type_info->flags&TYPEF_BASE) { // first, resolve this type's base if possible, or put it in the pending list
                        LTV *base=LT_get(index,type_info->base_str,HEAD,KEEP);
                        if (base) // we can link base immediately
                            LT_put(type_info_ltv,TYPE_BASE,HEAD,base);
                        else // we have to put it in the dependencies queue to try later
                            LT_put(dependencies,type_info->base_str,TAIL,type_info_ltv);
                    }

                    LTI *lti=LTI_resolve(dependencies,type_info->id_str,false); // now, see if this die resolves any in pending list
                    if (lti) {
                        void *link_base(CLL *lnk) {
                            LT_put(((LTVR *) lnk)->ltv,TYPE_BASE,HEAD,type_info_ltv);
                            LTVR_release(lnk);
                            return NULL;
                        }
                        CLL_map(&lti->ltvs,FWD,link_base);
                        RBN_release(&dependencies->sub.ltis,&lti->rbn,LTI_release); // purge type's id from dependencies
                    }
                }
                STRY(link2parent(name),"linking die to parent");
            done:
                return status;
            }

            STRY(!(type_info_ltv=LTV_new(NEW(TYPE_INFO),sizeof(TYPE_INFO),LT_OWN|LT_BIN|LT_CVAR|LT_TYPE)),"creating TYPE_INFO-type CVAR");
            type_info=(TYPE_INFO *) type_info_ltv->data;

            STRY(populate_type_info(dbg,die,type_info_ltv,&cu_data),"populating die type info");
            if (disqualify())
                LTV_release(type_info_ltv);
            else
                STRY(descend(),"processing type info");
            STRY(traverse_sibling(dbg,die,sib_op),"traversing sibling");
        done:
            return status;
        }
        return work_op(NULL,die);
    }

    char *filename=altname?altname:FORMATA(filename,module->len,"%s",module->data);
    do {
        STRY(traverse_cus(filename,init,&cu_data),"traversing module compute units");
        pass++;
    } while (!LTV_empty(dependencies));

    LTV_release(dependencies);
    resolve_symbols(module,altname?NULL:filename,index); // dlopen wants NULL for "this" module
    //traverse_types("/tmp/simple.dot",module);
    LTV_release(index);
    LTV_release(compile_units);
 done:
    return status;
}


char *Type_pushUVAL(TYPE_UVALUE *uval,char *buf)
{
    switch(uval->base.dutype)
    {
        case TYPE_INT1S:   sprintf(buf,"0x%x",  uval->int1s.val);   break;
        case TYPE_INT2S:   sprintf(buf,"0x%x",  uval->int2s.val);   break;
        case TYPE_INT4S:   sprintf(buf,"0x%x",  uval->int4s.val);   break;
        case TYPE_INT8S:   sprintf(buf,"0x%llx",uval->int8s.val);   break;
        case TYPE_INT1U:   sprintf(buf,"0x%x",  uval->int1u.val);   break;
        case TYPE_INT2U:   sprintf(buf,"0x%x",  uval->int2u.val);   break;
        case TYPE_INT4U:   sprintf(buf,"0x%x",  uval->int4u.val);   break;
        case TYPE_INT8U:   sprintf(buf,"0x%llx",uval->int8u.val);   break;
        case TYPE_FLOAT4:  sprintf(buf,"%g",    uval->float4.val);  break;
        case TYPE_FLOAT8:  sprintf(buf,"%g",    uval->float8.val);  break;
        case TYPE_FLOAT12: sprintf(buf,"%Lg",   uval->float12.val); break;
        default:           buf[0]=0; break;
    }
    return buf;
}

TYPE_UVALUE *Type_pullUVAL(TYPE_UVALUE *uval,char *buf)
{
    int tVar;
    switch(uval->base.dutype)
    {
        case TYPE_INT1S:   sscanf(buf,"%i",  &tVar);uval->int1s.val=tVar; break;
        case TYPE_INT2S:   sscanf(buf,"%i",  &tVar);uval->int2s.val=tVar; break;
        case TYPE_INT4S:   sscanf(buf,"%i",  &tVar);uval->int4s.val=tVar; break;
        case TYPE_INT8S:   sscanf(buf,"%lli",&uval->int8s.val);           break;
        case TYPE_INT1U:   sscanf(buf,"%i",  &tVar);uval->int1u.val=tVar; break;
        case TYPE_INT2U:   sscanf(buf,"%i",  &tVar);uval->int2u.val=tVar; break;
        case TYPE_INT4U:   sscanf(buf,"%i",  &tVar);uval->int4u.val=tVar; break;
        case TYPE_INT8U:   sscanf(buf,"%lli",&uval->int8u.val);           break;
        case TYPE_FLOAT4:  sscanf(buf,"%g",  &uval->float4.val);          break;
        case TYPE_FLOAT8:  sscanf(buf,"%g",  &uval->float8.val);          break;
        case TYPE_FLOAT12: sscanf(buf,"%Lg", &uval->float12.val);         break;
        default:           buf[0]=0; break;
    }
    return uval;
}


#define UVAL2VAR(uval,var)                                                      \
    {                                                                           \
        switch (uval.dutype)                                                    \
        {                                                                       \
            case TYPE_INT1S:   var=(typeof(var)) uval.int1s.val; break;         \
            case TYPE_INT2S:   var=(typeof(var)) uval.int2s.val; break;         \
            case TYPE_INT4S:   var=(typeof(var)) uval.int4s.val; break;         \
            case TYPE_INT8S:   var=(typeof(var)) uval.int8s.val; break;         \
            case TYPE_INT1U:   var=(typeof(var)) uval.int1u.val; break;         \
            case TYPE_INT2U:   var=(typeof(var)) uval.int2u.val; break;         \
            case TYPE_INT4U:   var=(typeof(var)) uval.int4u.val; break;         \
            case TYPE_INT8U:   var=(typeof(var)) uval.int8u.val; break;         \
            case TYPE_FLOAT4:  var=(typeof(var)) uval.float4.val; break;        \
            case TYPE_FLOAT8:  var=(typeof(var)) uval.float8.val; break;        \
            case TYPE_FLOAT12: var=(typeof(var)) uval.float12.val; break;       \
        }                                                                       \
    }

#define GETUVAL(member,type,uval)                                               \
    do {                                                                        \
        uval->member.dutype = type;                                             \
        uval->member.val = *(typeof(uval->member.val) *) cvar->data;            \
    } while(0)

#define GETUBITS(member,type,uval,bsize,boffset,issigned)                       \
    do {                                                                        \
        GETUVAL(member,type,uval);                                              \
        if (bsize) {                                                            \
            int shift = (sizeof(uval->member.val)*8)-(bsize)-(boffset);         \
            typeof(uval->member.val) mask = (1<<bsize)-1;                       \
            uval->member.val = (uval->member.val >> shift) & mask;              \
            if (issigned && (uval->member.val & (1<<bsize-1)))                  \
                uval->member.val |= ~mask;                                      \
        }                                                                       \
    } while(0)



TYPE_UTYPE Type_getUVAL(LTV *cvar,TYPE_UVALUE *uval)
{
    int status=0;
    LTV *type=NULL;
    STRY(!cvar || !uval,"validating params");
    STRY(!(type=ref_find_basic(LT_get(cvar,CVAR_TYPE,HEAD,KEEP))),"retrieving cvar basic type");
    TYPE_INFO *type_info=(TYPE_INFO *) type->data;

    ull size=type_info->bytesize;
    ull bitsize=type_info->bitsize;
    ull bitoffset=type_info->bitoffset;
    ull encoding;
    switch (type_info->tag) {
        case DW_TAG_member:           encoding=5;                   break; // bitfield
        case DW_TAG_enumeration_type: encoding=5;                   break;
        case DW_TAG_pointer_type:     encoding=7;                   break;
        case DW_TAG_base_type:        encoding=type_info->encoding; break;
        default: goto done;
    }

    BZERO(*uval);
    switch (encoding)
    {
        case DW_ATE_float:
            if      (size==4)  GETUVAL(float4,TYPE_FLOAT4,uval);
            else if (size==8)  GETUVAL(float8,TYPE_FLOAT8,uval);
            else if (size==12) GETUVAL(float12,TYPE_FLOAT12,uval);
            break;
        case DW_ATE_signed:
        case DW_ATE_signed_char:
            if      (size==1)  GETUBITS(int1s,TYPE_INT1S,uval,bitsize,bitoffset,1);
            else if (size==2)  GETUBITS(int2s,TYPE_INT2S,uval,bitsize,bitoffset,1);
            else if (size==4)  GETUBITS(int4s,TYPE_INT4S,uval,bitsize,bitoffset,1);
            else if (size==8)  GETUBITS(int8s,TYPE_INT8S,uval,bitsize,bitoffset,1);
            break;
        case DW_ATE_unsigned:
        case DW_ATE_unsigned_char:
            if      (size==1)  GETUBITS(int1u,TYPE_INT1U,uval,bitsize,bitoffset,0);
            else if (size==2)  GETUBITS(int2u,TYPE_INT2U,uval,bitsize,bitoffset,0);
            else if (size==4)  GETUBITS(int4u,TYPE_INT4U,uval,bitsize,bitoffset,0);
            else if (size==8)  GETUBITS(int8u,TYPE_INT8U,uval,bitsize,bitoffset,0);
            break;
        default:
            break;
    }
 done:
    return uval->base.dutype;
}

#define PUTUVAL(member,type,uval)                                               \
    do { *(typeof(uval->member.val) *) cvar->data = uval->member.val; } while(0)

#define PUTUBITS(member,type,uval,bsize,boffset,issigned)                       \
    do {                                                                        \
        if (bsize) {                                                            \
            TYPE_UVALUE tuval;                                                  \
            GETUVAL(member,type,(&tuval));                                      \
            int shift = (sizeof(uval->member.val)*8)-(bsize)-(boffset);         \
            typeof(uval->member.val) mask = ((1<<bsize)-1) << shift;            \
            uval->member.val = (uval->member.val << shift) & mask;              \
            uval->member.val |= tuval.member.val & ~mask;                       \
        }                                                                       \
        PUTUVAL(member,type,uval);                                              \
    } while(0)

int Type_putUVAL(LTV *cvar,TYPE_UVALUE *uval)
{
    int status=0;
    LTV *type=NULL;
    STRY(!cvar || !uval,"validating params");
    STRY(!(type=ref_find_basic(LT_get(cvar,CVAR_TYPE,HEAD,KEEP))),"retrieving cvar basic type");
    TYPE_INFO *type_info=(TYPE_INFO *) type->data;

    ull size=type_info->bytesize;
    ull bitsize=type_info->bitsize;
    ull bitoffset=type_info->bitoffset;
    ull encoding;
    switch (type_info->tag) {
        case DW_TAG_member:           encoding=5;                   break; // bitfield
        case DW_TAG_enumeration_type: encoding=5;                   break;
        case DW_TAG_pointer_type:     encoding=7;                   break;
        case DW_TAG_base_type:        encoding=type_info->encoding; break;
        default: goto done;
    }

    switch (encoding) {
        case 4: // float
            if      (size==4)  PUTUVAL(float4, TYPE_FLOAT4, uval);
            else if (size==8)  PUTUVAL(float8, TYPE_FLOAT8, uval);
            else if (size==12) PUTUVAL(float12,TYPE_FLOAT12,uval);
            break;
        case 5: // signed int
        case 6: // signed char
            if      (size==1)  PUTUBITS(int1s,TYPE_INT1S,uval,bitsize,bitoffset,1);
            else if (size==2)  PUTUBITS(int2s,TYPE_INT2S,uval,bitsize,bitoffset,1);
            else if (size==4)  PUTUBITS(int4s,TYPE_INT4S,uval,bitsize,bitoffset,1);
            else if (size==8)  PUTUBITS(int8s,TYPE_INT8S,uval,bitsize,bitoffset,1);
            break;
        case 7: // unsigned int
        case 8: // unsigned char
            if      (size==1)  PUTUBITS(int1u,TYPE_INT1U,uval,bitsize,bitoffset,0);
            else if (size==2)  PUTUBITS(int2u,TYPE_INT2U,uval,bitsize,bitoffset,0);
            else if (size==4)  PUTUBITS(int4u,TYPE_INT4U,uval,bitsize,bitoffset,0);
            else if (size==8)  PUTUBITS(int8u,TYPE_INT8U,uval,bitsize,bitoffset,0);
            break;
        default:
            break;
    }
 done:
    return status;
}


int Type_isBitField(TYPE_INFO *type_info) { return (type_info->bitsize || type_info->bitoffset); }
#define TYPE_INFO_NAME(p,ti)                                          \
    (((p)=Type_isBitField(ti)?                                        \
      FORMATA((p),256,"%s:%s:%s",attr_get((ti),TYPE_NAME),            \
              (ti)->DW_AT_bit_size,                                   \
              (ti)->DW_AT_bit_offset)                                 \
      :                                                               \
      attr_get((ti),TYPE_NAME)),                                      \
     (p))





// New name idea: GUT, for grand unified theory

LTV *ref_type_to_ffi_type(LTV *type,ffi_type *ft)
{
    int status=0;
    LTV *basic_type=NULL,*member_type=NULL,*cvar=NULL;
    TRYCATCH(!(basic_type=ref_find_basic(type)),0,done,"resolving basic type");
    TYPE_INFO *type_info=(TYPE_INFO *) basic_type->data;

    switch(type_info->tag) {
        case DW_TAG_base_type:
            break;
        default:
            break;
    }

    int size=type_info->bytesize;
 done:
    return status?NULL:cvar;
}





///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////







#if 0

long long *Type_getLocation(char *loc)
{
    return STRTOLLP(loc);
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

int Type_installTypeInfo(DICT *dict,void *data,TYPE_INFO *type_info)
{
    char buf[64];

    if (!strcmp(type_info->category,"base_type") ||
        !strcmp(type_info->category,"enumeration_type") ||
        !strcmp(type_info->category,"pointer_type"))
        jli_install(dict,Type_humanReadableVal(type_info,buf),type_info->name);

    return 0;
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
