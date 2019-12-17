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

#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>
#include <dlfcn.h> // dlopen/dlsym/dlclose
#include <arpa/inet.h>

#include <ffi.h>

#include "util.h"
#include "listree.h"
#include "reflect.h"
#include "vm.h"
#include "extensions.h"

LTV *cif_module=NULL;// initialized/populated during bootstrap

__attribute__((constructor))
static void init(void)
{
    Dl_info dl_info;
    dladdr((void *)init, &dl_info);
    fprintf(ERRFILE,CODE_RED "reflection module path is: %s" CODE_RESET "\n",dl_info.dli_fname);
    cif_module=LTV_init(NEW(LTV),(char *) dl_info.dli_fname,strlen(dl_info.dli_fname),LT_DUP|LT_RO);
    try_depth=1;
    cif_preview_module(cif_module);

    //LTI *lti=LTI_resolve(cif_module,"/home/jnyberg/j2/listree.c",false);
    //LTV_erase(cif_module,lti);
    //LTV_erase(cif_module,LTI_find(cif_module,"*/rbtree.c",false,LT_NONE));

    print_ltv(ERRFILE,CODE_RED,cif_module,CODE_RESET "\n",0);
    cif_curate_module(cif_module,true);
}

char *Type_pushUVAL(TYPE_UVALUE *uval,char *buf);
TYPE_UVALUE *Type_pullUVAL(TYPE_UVALUE *uval,char *buf);
TYPE_UTYPE Type_getUVAL(LTV *cvar,TYPE_UVALUE *uval);
int Type_putUVAL(LTV *cvar,TYPE_UVALUE *uval);
/////////////////////////////////////////////////////////////

LTV *attr_set(LTV *ltv,char *attr,char *val) { return LT_put(ltv,attr,TAIL,LTV_init(NEW(LTV),val,-1,LT_DUP)); }
LTV *attr_own(LTV *ltv,char *attr,char *val) { return LT_put(ltv,attr,TAIL,LTV_init(NEW(LTV),val,-1,LT_OWN)); }
LTV *attr_imm(LTV *ltv,char *attr,long long imm)    { return LT_put(ltv,attr,TAIL,LTV_init(NEW(LTV),(void *) imm,0,LT_IMM)); }

//extern char *attr_get(LTV *ltv,char *attr);
//extern void attr_del(LTV *ltv,char *attr);

extern char *attr_get(LTV *ltv,char *attr)
{
    LTV *attr_ltv=LT_get(ltv,attr,TAIL,KEEP);
    return attr_ltv?attr_ltv->data:NULL;
}

extern void attr_del(LTV *ltv,char *attr) { LTV_erase(ltv,LTI_resolve(ltv,attr,false)); }

// look up an attribute in one LTV, and look up it's value within another LTV.
extern char *attr_deref(LTV *ltv,char *attr,LTV *index)
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

typedef enum {
    RDW_none             =0x0,
    RDW_is_info          =0x1,
    RDW_traverse_sibs    =0x2,
} DIEWALK_FLAGS;

typedef int (*DIE_OP)(Dwarf_Debug dbg,Dwarf_Die die,DIEWALK_FLAGS flags);

int traverse_siblings(Dwarf_Debug dbg,Dwarf_Die die,DIE_OP op,DIEWALK_FLAGS flags)
{
    int status=0;
    Dwarf_Error error=0;
    Dwarf_Die sibling=0;

 iterate:
    if (die)
        STRY(op(dbg,die,flags),"operating on die");
    // else get first die

    TRY(dwarf_siblingof_b(dbg,die,flags&RDW_is_info,&sibling,&error),"retrieving dwarf_sibling");
    CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for DW_DLV_NO_ENTRY"); /* Done at this level. */
    CATCH(dwarf_errno(error)==DW_DLE_DBG_NO_CU_CONTEXT,1,goto done,"checking for DW_DLE_DBG_NO_CU_CONTEX %s",dwarf_errmsg(error));
    SCATCH("checking dwarf_siblingof");

    if (die)
        dwarf_dealloc(dbg,die,DW_DLA_DIE);

    if ((!die || (flags&RDW_traverse_sibs)) && (die=sibling))
        goto iterate;

 done:
    return status;
}

int traverse_child(Dwarf_Debug dbg,Dwarf_Die die,DIE_OP op,DIEWALK_FLAGS flags)
{
    int status=0;
    Dwarf_Error error=0;
    Dwarf_Die child=0;
    TRY(dwarf_child(die,&child,&error),"retrieving dwarf_child");
    CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking dwarf child's existence");
    SCATCH("checking dwarf_child for error");
    STRY(traverse_siblings(dbg,child,op,flags),"processing %s",flags&RDW_traverse_sibs?"children":"child");
 done:
    return status;
}

int traverse_cus(char *filename,DIE_OP op,CU_DATA *cu_data,DIEWALK_FLAGS flags)
{
    int status=0;
    Dwarf_Debug dbg;
    Dwarf_Error error=0;

    int read_cu_list() {
        CU_DATA cu_data_local;
        if (!cu_data) cu_data=&cu_data_local; // allow caller to not care
        Dwarf_Die die;


        while (1) {
            TRY(dwarf_next_cu_header_c(dbg,
                                       flags&RDW_is_info,
                                       &cu_data->header_length,
                                       &cu_data->version_stamp,
                                       &cu_data->abbrev_offset,
                                       &cu_data->address_size,
                                       &cu_data->length_size,
                                       &cu_data->extension_size,
                                       &cu_data->sig8,
                                       &cu_data->offset,
                                       &cu_data->next_cu_header_offset,
                                       &error),
                "reading next cu header");
            DWARF_ID(cu_data->next_cu_header_offset_str,cu_data->next_cu_header_offset);
            CATCH(status==DW_DLV_NO_ENTRY,0,goto done,"checking for no next cu header");
            CATCH(status!=DW_DLV_OK,status,goto done,"checking error dwarf_next_cu_header");

            static char alias[32];
            DWARF_ALIAS(alias,cu_data->sig8);
            DEBUG(fprintf(OUTFILE,CODE_BLUE "Read a CU header, is_info=%d, offset 0x%x sig8 %s" CODE_RESET "\n",flags&RDW_is_info,cu_data->offset,alias));

            STRY(traverse_siblings(dbg,NULL,op,flags),"processing cu die and sibs");
        }
    done:
        return status;
    }

    LTV *debug_link_filename=get_separated_debug_filename(filename);
    if (debug_link_filename) {
        fprintf(OUTFILE,"Using alt debug filename %s for %s\n",(char *) debug_link_filename->data,filename);
        filename=(char *) debug_link_filename->data;
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
    LTV_release(debug_link_filename);
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
    output_cu_field("%s",dwo_name);
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


int print_type_info(FILE *ofile,TYPE_INFO_LTV *type_info)
{
    int status=0;
    fprintf(ofile,"TYPE_INFO %s",type_info->id_str);
    const char *str=NULL;
    static char alias[32];
    DWARF_ALIAS(alias,type_info->sig8);
    dwarf_get_TAG_name(type_info->tag,&str);
    fprintf(ofile,"|%s",str+7);
    if (type_info->flags&TYPEF_BASE)       fprintf(ofile,"|base %s",        type_info->base_str);
    if (type_info->flags&TYPEF_CONSTVAL)   fprintf(ofile,"|constval 0x%x",  type_info->const_value);
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
    if (type_info->flags&TYPEF_OFFSET)     fprintf(ofile,"|offset %x",      type_info->offset);
    if (type_info->flags&TYPEF_IS_INFO)    fprintf(ofile,"|info");
    if (type_info->flags&TYPEF_SIGNATURE)  fprintf(ofile,"|sig %s",         alias);
    return status;
}

int dot_type_info(FILE *ofile,TYPE_INFO_LTV *type_info)
{
    int status=0;
    fprintf(ofile,CVAR_FORMAT " [shape=record label=\"{",type_info);
    print_type_info(ofile,type_info);
    fprintf(ofile,"}\"");
    if (type_info->flags&TYPEF_UPPERBOUND)
        fprintf(ofile," color=white");
    switch (type_info->tag) {
        case DW_TAG_compile_unit:     fprintf(ofile," style=filled fillcolor=red rank=max"); break;
        case DW_TAG_subprogram:       fprintf(ofile," style=filled fillcolor=orange"); break;
        case DW_TAG_subroutine_type:  fprintf(ofile," style=filled fillcolor=darkorange"); break;
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
        case DW_TAG_type_unit:        fprintf(ofile," style=filled fillcolor=darkred rank=min"); break;
    }
    fprintf(ofile,"]\n");
 done:
    return status;
}


LTV *cif_find_base(LTV *type,int tag)
{
    TYPE_INFO_LTV *type_info=NULL;
    while (type && (type->flags&LT_TYPE)) {
        type_info=(TYPE_INFO_LTV *) type;
        if (type_info->tag==tag)
            return type;
        type=LT_get(type,TYPE_BASE,HEAD,KEEP);
    }
    return NULL;
}

LTV *cif_find_symbolic(LTV *type)
{
    TYPE_INFO_LTV *type_info=NULL;
    while (type && (type->flags&LT_TYPE)) {
        type_info=(TYPE_INFO_LTV *) type;
        if (type_info->flags&TYPEF_SYMBOLIC)
            return type;
        type=LT_get(type,TYPE_BASE,HEAD,KEEP);
    }
    return NULL;
}

LTV *cif_find_concrete(LTV *type)
{
    TYPE_INFO_LTV *type_info=NULL;
    while (type && (type->flags&LT_TYPE)) {
        type_info=(TYPE_INFO_LTV *) type;
        switch (type_info->tag) {
            case DW_TAG_structure_type:
            case DW_TAG_union_type:
            case DW_TAG_enumeration_type:
            case DW_TAG_array_type:
            case DW_TAG_pointer_type:
            case DW_TAG_base_type:
                return type;
            default:
                type=LT_get(type,TYPE_BASE,HEAD,KEEP);
                break;
        }
    }
    return NULL;
}

LTV *cif_find_indexable(LTV *type)
{
    TYPE_INFO_LTV *type_info=NULL;
    while (type && (type->flags&LT_TYPE)) {
        type_info=(TYPE_INFO_LTV *) type;
        switch (type_info->tag) {
            case DW_TAG_structure_type:
            case DW_TAG_union_type:
            case DW_TAG_array_type:
            case DW_TAG_pointer_type:
                return type;
            default:
                type=LT_get(type,TYPE_BASE,HEAD,KEEP);
                break;
        }
    }
    return NULL;
}

LTV *cif_find_function(LTV *type)
{
    TYPE_INFO_LTV *type_info=NULL;
    while (type && (type->flags&LT_TYPE)) {
        type_info=(TYPE_INFO_LTV *) type;
        if (type_info->tag==DW_TAG_subprogram || type_info->tag==DW_TAG_subroutine_type)
            return type;
        type=LT_get(type,TYPE_BASE,HEAD,KEEP);
    }
    return NULL;
}

LTV *cif_get_child(LTV *type,char *childname)
{
    if (type->flags&LT_TYPE) {
        LTV *child=LT_get(type,childname,HEAD,KEEP);
        if (child && child->flags&LT_TYPE)
            return child;
    }
    return NULL;
}

LTV *cif_get_element(LTV *type,int index)
{
    // see Type_findMemberByIndex
}

LTV *cif_create_cvar(LTV *type,void *data,char *member)
{
    int status=0;
    LTV *basic_type=NULL,*member_type=NULL,*cvar=NULL;
    TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type->data;
    basic_type=member?cif_find_indexable(type):cif_find_concrete(type);
    int size=0;
    int is_array=0;
    if (!basic_type) {
        switch(type_info->tag) {
            case DW_TAG_subprogram:
            case DW_TAG_subroutine_type:
                size=sizeof(void *);
                break;
            default:
                goto done;
        }
    } else {
        TYPE_INFO_LTV *basic_type_info=(TYPE_INFO_LTV *) basic_type->data;

        switch(basic_type_info->tag) {
            case DW_TAG_structure_type:
            case DW_TAG_union_type:
                if (member) {
                    if ((status=!(member_type=cif_get_child(basic_type,member))))
                        goto done; // fail w/o message
                    TYPE_INFO_LTV *member_type_info=(TYPE_INFO_LTV *) member_type->data;
                    cvar=cif_create_cvar(member_type,data+member_type_info->data_member_location,NULL);
                    goto done;
                }
                break;
            case DW_TAG_array_type:
                is_array=LT_ARR; // when arranging ffi args, use address of data (i.e. pointer to array location)
                // fall thru
            case DW_TAG_pointer_type:
                if (member) {
                    errno=0;
                    char *end;
                    int index=strtol(member,&end,0);
                    if (errno || (end==member))
                        return NULL; // failed to find member, but we don't need to STRY/report it so just return right here
                    STRY(!(basic_type=cif_find_concrete(LT_get(basic_type,TYPE_BASE,HEAD,KEEP))),"dereferencing pointer type");
                    basic_type_info=(TYPE_INFO_LTV *) basic_type->data;
                    int offset=index*basic_type_info->bytesize;
                    if (!is_array) // data points to location of a pointer
                        data=*(void **) data;
                    // else data points to location of first element of array
                    cvar=cif_create_cvar(basic_type,data+offset,NULL);
                    goto done;
                }
                break;
            default:
                if ((status=member!=NULL))
                    goto done; // fail w/o message
                break;
        }

        size=basic_type_info->bytesize;
    }

    if (data)
        STRY(!(cvar=LTV_init(NEW(LTV),data,size,LT_BIN|LT_CVAR|is_array)),"creating reference cvar");
    else
        STRY(!(cvar=LTV_init(NEW(LTV),(void *) mymalloc(size),size,LT_OWN|LT_BIN|LT_CVAR|is_array)),"creating allocated cvar");
    LT_put(cvar,TYPE_BASE,HEAD,type);
 done:
    return status?NULL:cvar;
}

LTV *cif_assign_cvar(LTV *dst,LTV *src)
{
    int status=0;
    LTV *type=NULL;
    TYPE_UVALUE dst_uval={},src_uval={};
    STRY(!Type_getUVAL(dst,&dst_uval),"testing cvar dest compatibility");
    if (Type_getUVAL(src,&src_uval))
        Type_putUVAL(dst,&src_uval);
    else
        Type_putUVAL(dst,Type_pullUVAL(&dst_uval,src->data));
 done:
    return status?NULL:dst;
}

int cif_dump_cvar(FILE *ofile,LTV *cvar,int depth)
{
    int status=0;
    LTV *type;
    STRY(!(type=LT_get(cvar,TYPE_BASE,HEAD,KEEP)),"validating cvar via type");
    CLL queue;
    CLL_init(&queue);
    LTV_enq(&queue,cif_create_cvar(type,cvar->data,NULL),TAIL); // copy cvar so we don't mess with it

    int traverse_array(LTV *cvar,int count)
    {
        int status=0;
        STRY(!(type=LT_get(cvar,TYPE_BASE,HEAD,KEEP)),"looking up cvar type");
        TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type->data;
        for (int i=0;i<count;i++) {
            cif_dump_cvar(ofile,cvar,depth+4);
            cvar->data+=type_info->bytesize;
        }
    done:
        return status;
    }
    int process_type_info(LTV *cvar) {
        int status=0;
        LTV *type=NULL;

        void *type_info_op(CLL *lnk) {
            LTV *ltv=((LTVR *) lnk)->ltv;
            TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) ltv;
            switch (type_info->tag) {
                case DW_TAG_member:
                    LTV_enq(&queue,cif_create_cvar(ltv,cvar->data+type_info->data_member_location,NULL),TAIL);
                    break;
                default:
                    fprintf(ofile,CODE_RED "child tag %d unimplemented" CODE_RESET "\n",type_info->tag);
                    break;
            }
        done:
            return status?NON_NULL:NULL;
        }

        STRY(!(type=LT_get(cvar,TYPE_BASE,HEAD,KEEP)),"looking up cvar type");
        TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type->data;

        char *name=attr_get(type,TYPE_SYMB);
        {
            const char *str=NULL;
            dwarf_get_TAG_name(type_info->tag,&str);
            fprintf(ofile,"%*c%s \"%s\" ",depth*4,' ',str+7,name);

            switch(type_info->tag) {
                case DW_TAG_union_type:
                case DW_TAG_structure_type: {
                    LTI *children=NULL;
                    if ((children=LTI_resolve(type,TYPE_LIST,false)))
                        CLL_map(&children->ltvs,FWD,type_info_op);
                    break;
                }
                case DW_TAG_subprogram:
                case DW_TAG_subroutine_type:
                    fprintf(ofile,"0x%x",type->data);
                    break;
                case DW_TAG_pointer_type:
                    fprintf(ofile,"0x%x",*(void **) type->data);
                    break;
                case DW_TAG_array_type: {
                    TYPE_INFO_LTV *base_info=NULL;
                    if (type_info->flags&TYPEF_BASE) // link to base type
                        STRY(!(base_info=(TYPE_INFO_LTV *) LT_get(&type_info->ltv,TYPE_BASE,HEAD,KEEP)),"looking up base die for %s",type_info->id_str);
                    char *base_symb=base_info && (base_info->flags&TYPEF_SYMBOLIC)? attr_get(&base_info->ltv,TYPE_SYMB):NULL;
                    if (base_symb) {
                        LTV *subrange_ltv=LT_get(&type_info->ltv,"subrange type",HEAD,KEEP);
                        TYPE_INFO_LTV *subrange=subrange_ltv?(TYPE_INFO_LTV *) subrange_ltv->data:NULL;
                        if (subrange && subrange->flags&TYPEF_UPPERBOUND) {
                            fprintf(ofile,"\n");
                            LTV *tcvar=cif_create_cvar(&base_info->ltv,cvar->data,NULL);
                            traverse_array(tcvar,subrange->upper_bound+1);
                            LTV_release(tcvar);
                        }
                        else
                            fprintf(ofile,"(unbounded array of %s)",base_symb);
                    }
                    break;
                }
                case DW_TAG_enumeration_type:
                    fprintf(ofile,"(enum display unimplemented)");
                    break;
                case DW_TAG_enumerator:
                    fprintf(ofile,"0x%x",type_info->const_value);
                    break;
                case DW_TAG_member:
                    if (type_info->flags&TYPEF_BYTESIZE)
                        ; /* fall thru! */
                    else {
                        LTV_enq(&queue,cif_create_cvar(cif_find_concrete(type),cvar->data,NULL),HEAD);
                        break;
                    }
                case DW_TAG_base_type: {
                    TYPE_UVALUE uval={};
                    char buf[64];
                    if (Type_getUVAL(cvar,&uval))
                        fprintf(ofile,"%s (%p)",Type_pushUVAL(&uval,buf),cvar->data);
                    break;
                }
                default:
                    LTV_enq(&queue,cif_create_cvar(cif_find_concrete(type),cvar->data,NULL),HEAD);
                    break;
            }
        }
    done:
        fprintf(ofile,"\n");
        return status;
    }

    fprintf(ofile,"CVAR:\n");
    while ((cvar=LTV_deq(&queue,HEAD))) {
        process_type_info(cvar);
        LTV_release(cvar);
    }

 done:
    return status;
}


int cif_print_cvar(FILE *ofile,LTV *ltv,int depth)
{
    int status=0;
    if (ltv->flags&LT_TYPE) // special case
        print_type_info(ofile,(TYPE_INFO_LTV *) ltv);
    else if (ltv->flags&LT_FFI)
        fprintf(OUTFILE,"Flagged as FFI");
    else if (ltv->flags&LT_CIF)
        fprintf(OUTFILE,"Flagged as CIF");
    else
        cif_dump_cvar(ofile,ltv,depth); // use reflection!!!!!
 done:
    return status;
}

int cif_dot_cvar(FILE *ofile,LTV *ltv)
{
    int status=0;
    if (ltv->flags&LT_TYPE)
        dot_type_info(ofile,(TYPE_INFO_LTV *) ltv);
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

int cif_die_offset(Dwarf_Die die,Dwarf_Off *offset,Dwarf_Error *error) {
    int status=0;
    Dwarf_Off cu_offset=0,die_offset=0;
    STRY(dwarf_CU_dieoffset_given_die(die,&cu_offset,error),"getting cu offset");
    STRY(dwarf_die_CU_offset(die,&cu_offset,error),"getting cu offset");
    *offset=cu_offset+die_offset;
 done:
    return status;
}

#define IF_OK(cond,followup) if (cond==DW_DLV_OK) followup

int populate_type_info(Dwarf_Debug dbg,Dwarf_Die die,TYPE_INFO_LTV *type_info,CU_DATA *cu_data)
{
    int status=0;
    Dwarf_Error error=0;

    char *diename = NULL;

    STRY(!type_info || !cu_data,"validating params");

    STRY(die==NULL,"testing for null die");
    Dwarf_Off goff;
    STRY(dwarf_dieoffset(die,&goff,&error),"getting global die offset");
    DWARF_ID(type_info->id_str,goff);
    DWARF_ID(cu_data->offset_str,goff);
    STRY(dwarf_tag(die,&type_info->tag,&error),"getting die tag");

    if (dwarf_get_die_infotypes_flag(die))
        type_info->flags|=TYPEF_IS_INFO;

    switch (type_info->tag)
    {
        case DW_TAG_type_unit:
        case DW_TAG_pointer_type:
        case DW_TAG_array_type:
        case DW_TAG_volatile_type:
        case DW_TAG_const_type:
        case DW_TAG_restrict_type:
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
            fprintf(OUTFILE,CODE_RED "Unrecognized tag 0x%x\n" CODE_RESET,type_info->tag);
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
            type_info->tag=0; // reject
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
        Dwarf_Off voffset;
        Dwarf_Sig8 vsig8;

        switch (vshort) {
            case DW_AT_name: // string
                break;
            case DW_AT_type: // global_formref
                STRY(dwarf_whatform(*attr,&vshort,&error),"getting attr form");
                IF_OK(dwarf_global_formref(*attr,&type_info->base,&error),type_info->flags|=TYPEF_BASE);
                DWARF_ID(type_info->base_str,type_info->base);
                IF_OK(dwarf_formsig8(*attr,&type_info->sig8,&error),type_info->flags|=(TYPEF_BASE|TYPEF_SIGNATURE));
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
            case DW_AT_signature:
                IF_OK(dwarf_formsig8(*attr,&type_info->sig8,&error),type_info->flags|=TYPEF_SIGNATURE);
                break;
            case DW_AT_sibling:
            case DW_AT_high_pc:
            case DW_AT_decl_column:
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
            default: {
                fprintf(OUTFILE,CODE_RED "Unrecognized attr 0x%x\n",vshort);
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
                fprintf(OUTFILE,"form %d (%s) ",vshort,vcstr);

                STRY(dwarf_whatform_direct(*attr,&vshort,&error),"getting attr form_direct");
                STRY(dwarf_get_FORM_name(vshort,&vcstr),"getting attr form_direct name");
                fprintf(OUTFILE,"form_direct %d (%s) ",vshort,vcstr);

                IF_OK(dwarf_formref(*attr,&voffset,&error),       fprintf(OUTFILE,"formref 0x%"        DW_PR_DSx " ",voffset));
                IF_OK(dwarf_global_formref(*attr,&voffset,&error),fprintf(OUTFILE,"global_formref 0x%" DW_PR_DSx " ",voffset));
                IF_OK(dwarf_formaddr(*attr,&vaddr,&error),        fprintf(OUTFILE,"addr 0x%"           DW_PR_DUx " ",vaddr));
                IF_OK(dwarf_formflag(*attr,&vbool,&error),        fprintf(OUTFILE,"flag %"             DW_PR_DSd " ",vbool));
                IF_OK(dwarf_formudata(*attr,&vuint,&error),       fprintf(OUTFILE,"udata %"            DW_PR_DUu " ",vuint));
                IF_OK(dwarf_formsdata(*attr,&vint,&error),        fprintf(OUTFILE,"sdata %"            DW_PR_DSd " ",vint));
                IF_OK(dwarf_formblock(*attr,&vblock,&error),      fprintf(OUTFILE,"block 0x%"          DW_PR_DUx " ",vblock->bl_len));
                IF_OK(dwarf_formstring(*attr,&vstr,&error),       fprintf(OUTFILE,"string %s ",                      vstr));
                IF_OK(dwarf_formsig8(*attr,&vsig8,&error),        fprintf(OUTFILE,"addr %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d ",
                                                                         vsig8.signature[0],vsig8.signature[1],vsig8.signature[2],vsig8.signature[3],
                                                                         vsig8.signature[4],vsig8.signature[5],vsig8.signature[6],vsig8.signature[7]));
                fprintf(OUTFILE,"\n");
                break;
            }
        }

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
                        case DW_OP_GNU_push_tls_address: // THREAD LOCAL STORAGE
                            type_info->tag=0; // disqualify TLS variables
                            break;
                        case DW_OP_consts: case DW_OP_const1s: case DW_OP_const2s: case DW_OP_const4s: case DW_OP_const8s: // (Dwarf_Signed) llbuf->ld_s[j].lr_number
                        case DW_OP_constu: case DW_OP_const1u: case DW_OP_const2u: case DW_OP_const4u: case DW_OP_const8u: // llbuf->ld_s[j].lr_number
                        case DW_OP_fbreg: // (Dwarf_Signed) llbuf->ld_s[j].lr_number
                        case DW_OP_bregx: // fprintf(OUTFILE," bregx %" DW_PR_DUu " + (%" DW_PR_DSd ") ",llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2);
                        case DW_OP_regx: // fprintf(OUTFILE," regx %" DW_PR_DUu " + (%" DW_PR_DSd ") ",llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2);
                        case DW_OP_pick:
                        case DW_OP_plus_uconst:
                        case DW_OP_piece:
                        case DW_OP_deref_size:
                        case DW_OP_xderef_size:
                        case DW_OP_GNU_uninit:
                        case DW_OP_GNU_encoded_addr:
                        case DW_OP_GNU_implicit_pointer:
                        case DW_OP_GNU_entry_value:
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
                        case DW_OP_GNU_parameter_ref: // unreferenced parameter
                            // fprintf(OUTFILE," Ingnored DW_OP 0x%x n 0x%x n2 0x%x offset 0x%x",llbuf->ld_s[j].lr_atom,llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2,llbuf->ld_s[j].lr_offset);
                            break;
                        default:
                            fprintf(OUTFILE," Unrecognized DW_OP 0x%x n 0x%x n2 0x%x offset 0x%x",llbuf->ld_s[j].lr_atom,llbuf->ld_s[j].lr_number,llbuf->ld_s[j].lr_number2,llbuf->ld_s[j].lr_offset);
                            fprintf(OUTFILE,CODE_RED " lowpc %" DW_PR_DUx " hipc %"  DW_PR_DUx " ld_section_offset %" DW_PR_DUx " ld_from_loclist %s ld_cents %d ",
                                   llbuf->ld_lopc,llbuf->ld_hipc,llbuf->ld_section_offset,llbuf->ld_from_loclist?"debug_loc":"debug_info",llbuf->ld_cents);
                            fprintf(OUTFILE,CODE_RESET "\n");
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

    // Disqualify certain nodes
    switch (type_info->tag) {
        case 0: // populate_type_info rejected it
        case  DW_TAG_label:
            type_info->tag=0;
        case DW_TAG_variable:
        case DW_TAG_subprogram:
            if (type_info->depth>1)
                type_info->tag=0;
            else if (!(type_info->flags&TYPEF_EXTERNAL))
                type_info->tag=0;
            break;
        default:
            break;
    }

 done:
    return status;
}

char *get_diename(Dwarf_Debug dbg,Dwarf_Die die)
{
    int status=0;
    Dwarf_Error error=0;
    char *diename=NULL;
    STRY(dwarf_diename(die,&diename,&error)==DW_DLV_ERROR,"checking dwarf_diename");
    char *type_info_name=diename?bufdup(diename,-1):NULL;
    dwarf_dealloc(dbg,diename,DW_DLA_STRING);
 done:
    return (status || !type_info_name)?NULL:type_info_name;
}


int _cif_preview_module(LTV *module) // just put the cu name under module
{
    CU_DATA cu_data;
    int op(Dwarf_Debug dbg,Dwarf_Die die,DIEWALK_FLAGS flags) {
        int status=0;
        Dwarf_Error error=0;
        char *cu_name=NULL;
        Dwarf_Off offset;
        STRY(dwarf_CU_dieoffset_given_die(die,&offset,&error),"getting global die offset");
        STRY(!(cu_name=get_diename(dbg,die)),"looking up cu die name");
        STRY(!attr_imm(module,cu_name,(long long) offset),"adding cu name to list of compute units");
    done:
        return status;
    }
    char *filename=PRINTA(filename,module->len,module->data);
    return
        traverse_cus(filename,op,&cu_data,RDW_is_info);
}
int cif_preview_module(LTV *module) { return _cif_preview_module(module); }


int cif_dump_module(char *ofilename,LTV *module)
{
    int status=0;
    FILE *ofile=fopen(ofilename,"w");

    void *traverse_types(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        listree_acyclic(lti,ltvr,ltv,depth,flags);
        if ((*flags&LT_TRAVERSE_LTV) && (*ltv)->flags&LT_CVAR && !((*ltv)->flags&LT_TYPE)) {
            TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) (*ltv);
            if (type_info->flags&TYPEF_BASE)
                fprintf(ofile,"\"%s\" -> \"%s\"\n",type_info->id_str,type_info->base_str);
            if (!(*flags)&LT_TRAVERSE_HALT)
                fprintf(ofile,"\"%s\" [label=\"%s\"]\n",type_info->id_str,attr_get((*ltv),TYPE_SYMB));
        }
        return NULL;
    }

    // simple dump of just typenames linked to their base types
    fprintf(ofile,"digraph iftree\n{\ngraph [ratio=compress, concentrate=true] node [shape=record] edge []\n");
    STRY(ltv_traverse(module,traverse_types,NULL)!=NULL,"traversing module");
    fprintf(ofile,"}\n");
 done:
    fclose(ofile);
    return status;
}

extern void dump_macros(Dwarf_Debug dbg, Dwarf_Die cu_die);

int _cif_curate_module(LTV *module,int bootstrap)
{
    int status=0;
    CU_DATA cu_data;

    LTV *index=LTV_NULL,*aliases=LTV_NULL;
    char *filename=FORMATA(filename,module->len,"%s",module->data);

    LTV *resolve_alias(LTV *type_info_ltv) {
        LTV *base=NULL;
        TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type_info_ltv;
        if ((type_info->flags&TYPEF_BASE) && (type_info->flags&TYPEF_SIGNATURE)) {
            static char alias[32];
            DWARF_ALIAS(alias,type_info->sig8);
            base=LT_get(aliases,alias,HEAD,KEEP);
            DEBUG(fprintf(OUTFILE,"resolve %s ref to %s found %x\n",type_info->id_str,alias,base));
        }
        return base;
    }

    int resolve_symbols()
    {
        int status=0;
        void *dlhandle=NULL;

        int derive_symbolic_name(TYPE_INFO_LTV *type_info,int post) {
            int status=0;
            if (!type_info->tag) return 0;

            TRYCATCH(type_info->flags&TYPEF_SYMBOLIC,0,done,"checking if symbolic name already derived");
            TYPE_INFO_LTV *base_info=NULL;
            if (type_info->flags&TYPEF_BASE) // link to base type
                STRY(!(base_info=(TYPE_INFO_LTV *) LT_get(&type_info->ltv,TYPE_BASE,HEAD,KEEP)),"looking up base die for %s",type_info->id_str);

            char *type_name=attr_get(&type_info->ltv,TYPE_NAME);
            char *base_symb=base_info && (base_info->flags&TYPEF_SYMBOLIC)? attr_get(&base_info->ltv,TYPE_SYMB):NULL;
            char *composite_name=NULL;

            LTV *dedup_base() {
                if (base_info && base_symb) { // dedup types; to get here, base must have already been categorized
                    TYPE_INFO_LTV *symb_base=(TYPE_INFO_LTV *) LT_get(module,base_symb,HEAD,KEEP);
                    if (symb_base && symb_base!=base_info) { // may already be correct
                        attr_del(&type_info->ltv,TYPE_BASE);
                        LT_put(&type_info->ltv,TYPE_BASE,TAIL,&symb_base->ltv);
                        strncpy(type_info->base_str,symb_base->id_str,TYPE_IDLEN);

                        base_info=symb_base; // update this to reflect new base
                        base_symb=attr_get(&base_info->ltv,TYPE_SYMB);
                    }
                }
            }

            TYPE_INFO_LTV *categorize_symbolic(char *sym) {
                int tag_category(TYPE_INFO_LTV *type) {
                    switch (type->tag) {
                        case DW_TAG_subprogram: case DW_TAG_subroutine_type: return DW_TAG_subprogram;
                        default: return type->tag;
                    }
                }

                TYPE_INFO_LTV *deduped=NULL;
                if (sym) {
                    type_info->flags|=TYPEF_SYMBOLIC;
                    attr_set(&type_info->ltv,TYPE_SYMB,sym);
                    const char *is;
                    dwarf_get_TAG_name(type_info->tag,&is);
                    LTV *sym_ltv=LT_get(module,sym,HEAD,KEEP); // see if symbol already exists
                    if (sym_ltv && sym_ltv->flags&LT_TYPE) {
                        TYPE_INFO_LTV *sym_type_info=(TYPE_INFO_LTV *) sym_ltv;
                        if (tag_category(type_info)==tag_category(sym_type_info))
                            deduped=sym_type_info;
                        else {
                            const char *already,*named;
                            named=attr_get(&sym_type_info->ltv,TYPE_NAME);
                            dwarf_get_TAG_name(sym_type_info->tag,&already);
                            fprintf(OUTFILE,CODE_RED "conflict for \"%s\": %s (%s) \"%s\" vs. (installed)  %s (%s) \"%s\"" CODE_RESET "\n",
                                   sym,is,type_info->id_str,type_name,already,sym_type_info->id_str,named);
                        }
                    }
                }

                if (!deduped) {
                    DEBUG(fprintf(OUTFILE,"installing symbolic type_info %s (%s)\n",sym,type_info->id_str));
                    LT_put(module,sym,HEAD,&type_info->ltv);
                }

                return deduped?deduped:type_info;
            }

            if (post)
                dedup_base();

            switch(type_info->tag) {
                case 0:
                    break;
                case DW_TAG_structure_type:
                    if (post && type_name)
                        categorize_symbolic(FORMATA(composite_name,strlen(type_name),"struct %s",type_name));
                    break;
                case DW_TAG_union_type:
                    if (post && type_name)
                        categorize_symbolic(FORMATA(composite_name,strlen(type_name),"union %s",type_name));
                    break;
                case DW_TAG_enumeration_type:
                    if (type_name)
                        categorize_symbolic(FORMATA(composite_name,strlen(type_name),"enum %s",type_name));
                    break;
                case DW_TAG_pointer_type:
                    if (post) {
                        if (!(type_info->flags&TYPEF_BASE))
                            base_symb="void";
                        if (base_symb)
                            categorize_symbolic(FORMATA(composite_name,strlen(base_symb),"(%s)*",base_symb));
                    }
                    break;
                case DW_TAG_array_type:
                    if (post) {
                        if (base_symb) {
                            LTV *subrange_ltv=LT_get(&type_info->ltv,"subrange type",HEAD,KEEP);
                            TYPE_INFO_LTV *subrange=subrange_ltv?(TYPE_INFO_LTV *) subrange_ltv->data:NULL;
                            if (subrange && subrange->flags&TYPEF_UPPERBOUND) {
                                if (base_info && (base_info->flags&TYPEF_BYTESIZE)) {
                                    type_info->bytesize=base_info->bytesize * (subrange->upper_bound+1);
                                    type_info->flags|=TYPEF_BYTESIZE;
                                }
                                categorize_symbolic(FORMATA(composite_name,strlen(base_symb)+20,"(%s)[%d]",base_symb,subrange->upper_bound+1));
                            }
                            else
                                categorize_symbolic(FORMATA(composite_name,strlen(base_symb),"(%s)[]",base_symb));
                        }
                    }
                    break;
                case DW_TAG_volatile_type:
                    if (post && base_symb)
                        categorize_symbolic(FORMATA(composite_name,strlen(base_symb),"volatile %s",base_symb));
                    break;
                case DW_TAG_const_type:
                    if (post && base_symb)
                        categorize_symbolic(FORMATA(composite_name,strlen(base_symb),"const %s",base_symb));
                    break;
                case DW_TAG_restrict_type:
                    if (post && base_symb)
                        categorize_symbolic(FORMATA(composite_name,strlen(base_symb),"restrict %s",base_symb));
                    break;
                case DW_TAG_base_type:
                case DW_TAG_enumerator:
                    if (type_name)
                        categorize_symbolic(FORMATA(composite_name,strlen(type_name),"%s",type_name));
                    break;
                case DW_TAG_typedef:
                    if (post) {
                        if (type_name)
                            categorize_symbolic(FORMATA(composite_name,strlen(type_name),"%s",type_name));
                        else if (base_symb) // anonymous typedef
                            categorize_symbolic(FORMATA(composite_name,strlen(base_symb),"%s",base_symb));
                    }
                    break;
                case DW_TAG_subprogram:
                case DW_TAG_subroutine_type:
                    if (post) {
                        char signature[1024];
                        char *bufloc=signature;
                        int count=0;
                        int marshaller(char *name,LTV *type) {
                            count++;
                            LTV *base=cif_find_symbolic(type);
                            bufloc+=sprintf(bufloc,"%s,",attr_get(base,TYPE_SYMB));
                            return 0;
                        }
                        bufloc+=sprintf(bufloc,"%s(*)(",base_symb);
                        STRY(cif_args_marshal(&type_info->ltv,FWD,marshaller),"marshalling ffi args"); // pre-
                        bufloc+=sprintf(bufloc-(count?1:0),")");
                        TYPE_INFO_LTV *cvar_type=categorize_symbolic(signature); // GLOBAL!

                        if (type_name && !LT_get(module,type_name,HEAD,KEEP)) {
                            void *addr=NULL;
                            if (dlhandle) {
                                dlerror(); // reset
                                if ((addr=dlsym(dlhandle,type_name)))
                                    LT_put(module,type_name,TAIL,cif_create_cvar(&cvar_type->ltv,addr,NULL));
                                else
                                    DEBUG(fprintf(ERRFILE,"dlsym error: handle %x %s\n",dlhandle,dlerror()));
                            } else
                                fprintf(ERRFILE,"no dlhandle for function %s\n",type_name);
                        }
                    }
                    break;
                case DW_TAG_variable:
                    if (post) {
                        if (type_name && !LT_get(module,type_name,HEAD,KEEP) && base_info) { // GLOBAL!
                            void *addr=NULL;
                            if (dlhandle) {
                                dlerror(); // reset
                                if ((addr=dlsym(dlhandle,type_name)))
                                    LT_put(module,type_name,TAIL,cif_create_cvar(&base_info->ltv,addr,NULL));
                                else
                                    DEBUG(fprintf(ERRFILE,"dlsym error: handle %x %s\n",dlhandle,dlerror()));
                            } else
                                fprintf(ERRFILE,"no dlhandle for variable %s\n",type_name);
                        }
                    }
                    break;
                case DW_TAG_subrange_type:
                    if (type_name) // still want to dedup!
                        categorize_symbolic(FORMATA(composite_name,strlen(type_name),"%s",type_name));
                    break;
                case DW_TAG_member:
                    if (type_name && post)
                        attr_set(&type_info->ltv,TYPE_SYMB,type_name); // let the member have it's own name, even if it's not a "symbolic" type
                    break;
                case DW_TAG_formal_parameter:
                case DW_TAG_unspecified_parameters: // varargs
                case DW_TAG_compile_unit:
                case DW_TAG_type_unit:
                    break;
                default: // no name
                    break;
            }
        done:
            return status;
        }

        void *resolve_types(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
            if (!listree_acyclic(lti,ltvr,ltv,depth,flags)) {
                if (((*flags)&LT_TRAVERSE_LTV) && (*ltv)->flags&LT_TYPE) {
                    derive_symbolic_name((TYPE_INFO_LTV *) (*ltv),(*flags)&LT_TRAVERSE_POST);
                }
            }
            return NULL;
        }

        char *f=bootstrap?NULL:filename;
        if (!(dlhandle=dlopen(f,RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE | RTLD_DEEPBIND)))
            fprintf(OUTFILE,"failed while dlopen'ing %s; continuing without resolving global functions/variables\n",dlerror());

        STRY(ltv_traverse(index,resolve_types,resolve_types)!=NULL,"linking symbolic names"); // links symbols on pre- and post-passes
        if (dlhandle)
            STRY(dlclose(dlhandle),"closing shared library");
        //graph_types_to_file("/tmp/types.dot",types);

    done:
        return status;
    }

    int curate_die(Dwarf_Debug dbg,Dwarf_Die die,DIEWALK_FLAGS flags) {
        int work_op(LTV *parent,Dwarf_Die die,int depth) { // propagates parentage through the stateless DIE_OP calls
            int status=0;
            Dwarf_Error error=0;
            TYPE_INFO_LTV *type_info=NULL;
            TYPE_INFO_LTV *parent_type_info=(TYPE_INFO_LTV *) parent;

            void read_cu_macros() { // inspired by https://github.com/tomhughes/libdwarf/blob/master/libdwarf/checkexamples.c
                int status=0;

                void macro_define(char *macro) {
                    int len=strlen(macro);
                    void advance(int adv) { macro+=adv; len-=adv; }
                    int namelen=series(macro,len,NULL,WHITESPACE,"()");
                    if (namelen<len) {
                        char *name=PRINTA(name,namelen,macro);
                        advance(namelen);
                        advance(series(macro,len,WHITESPACE,NULL,NULL));
                        if (len) {
                            //fprintf(OUTFILE,name[namelen-1]!=')'?CODE_BLUE:CODE_GREEN);
                            //fprintf(OUTFILE,"%s" CODE_RED " %s" CODE_RESET " (%d) \n",name,macro,len);
                            if (name[namelen-1]!=')' && !LT_get(module,name,TAIL,KEEP))
                                LT_put(module,name,TAIL,LTV_init(NEW(LTV),macro,len,LT_DUP));
                        }
                    }
                }
                void macro_undefine(char *macro) { /* fprintf(OUTFILE,"undefine %s\n",macro); */ }

                int lres = 0;
                Dwarf_Unsigned version = 0;
                Dwarf_Macro_Context macro_context = 0;
                Dwarf_Unsigned macro_unit_offset = 0;
                Dwarf_Unsigned number_of_ops = 0;
                Dwarf_Unsigned ops_total_byte_len = 0;
                Dwarf_Bool is_primary = TRUE;
                unsigned k = 0;
                Dwarf_Error err = 0;

                for(;;) {
                    if (is_primary) {
                        TRY(dwarf_get_macro_context(die,&version,&macro_context,&macro_unit_offset,&number_of_ops,&ops_total_byte_len,&err),"getting primary macro context");
                        is_primary = FALSE;
                    } else {
                        LTV *macro_ltv=LT_get(module,"macro context",HEAD,POP);
                        if (!macro_ltv)
                            break;
                        macro_unit_offset=(Dwarf_Unsigned) macro_ltv->data;
                        LTV_release(macro_ltv);
                        TRY(dwarf_get_macro_context_by_offset(die,macro_unit_offset,&version,&macro_context,&number_of_ops,&ops_total_byte_len,&err),"getting macro context by offset");
                    }

                    CATCH(status==DW_DLV_NO_ENTRY,0,break,"exhausting macro context");
                    SCATCH("processing macro context");

                    for (k = 0; k < number_of_ops; ++k) {
                        Dwarf_Unsigned  section_offset = 0;
                        Dwarf_Half      macro_operator = 0;
                        Dwarf_Half      forms_count = 0;
                        const Dwarf_Small *formcode_array = 0;
                        Dwarf_Unsigned  line_number = 0;
                        Dwarf_Unsigned  index = 0;
                        Dwarf_Unsigned  offset =0;
                        const char    * macro_string =0;
                        int lres = 0;

                        STRY(DW_DLV_OK!=dwarf_get_macro_op(macro_context,k, &section_offset,&macro_operator,&forms_count, &formcode_array,&err),"calling dwarf_get_macro_op");

                        switch(macro_operator) {
                            case DW_MACRO_define:
                            case DW_MACRO_define_strp:
                            case DW_MACRO_define_strx:
                            case DW_MACRO_define_sup:
                                STRY(DW_DLV_OK!=dwarf_get_macro_defundef(macro_context,k,&line_number,&index,&offset,&forms_count,&macro_string,&err),"calling dwarf_get_macro_defundef");
                                macro_define((char *) macro_string);
                                break;
                            case DW_MACRO_undef:
                            case DW_MACRO_undef_strp:
                            case DW_MACRO_undef_strx:
                            case DW_MACRO_undef_sup:
                                STRY(DW_DLV_OK!=dwarf_get_macro_defundef(macro_context,k,&line_number,&index,&offset,&forms_count,&macro_string,&err),"calling dwarf_get_macro_defundef");
                                macro_undefine((char *) macro_string);
                                break;
                            case DW_MACRO_import:
                                STRY(DW_DLV_OK!=dwarf_get_macro_import(macro_context,k,&offset,&err),"calling dwarf_get_macro_import");
                                LT_put(module,"macro context",TAIL,LTV_init(NEW(LTV),(void *) offset,0,LT_IMM));
                                break;
                            case DW_MACRO_import_sup:
                                STRY(DW_DLV_OK!=dwarf_get_macro_import(macro_context,k,&offset,&err),"calling dwarf_get_macro_import");
                                fprintf(OUTFILE,"import_sup(?)\n");
                                break;
                            default:
                                break;
                        }
                    }
                done:
                    if (macro_context)
                        dwarf_dealloc_macro_context(macro_context);
                    if (status)
                        return;
                    macro_context = 0;
                }
            }

            int child_op(Dwarf_Debug dbg,Dwarf_Die die,DIEWALK_FLAGS flags) { return work_op(&type_info->ltv,die,depth+1); }

            int link2parent(char *name) {
                int status=0;
                switch(type_info->tag) {
                    case 0: // rejected
                        break;
                    case DW_TAG_compile_unit:
                        if (!LTV_empty(&type_info->ltv) && name)
                            STRY(!LT_put(module,name,TAIL,&type_info->ltv),"linking cu to module");
                        break;
                    case DW_TAG_subprogram:
                    case DW_TAG_variable:
                        if (parent && name)
                            STRY(!LT_put(parent,name,TAIL,&type_info->ltv),"linking extern subprogram/variable to parent");
                        break;
                    case DW_TAG_subrange_type:
                        if (parent)
                            STRY(!LT_put(parent,"subrange type",TAIL,&type_info->ltv),"linking subrange type to parent");
                        break;
                    case DW_TAG_member:
                    case DW_TAG_formal_parameter:
                        if (parent) {
                            STRY(!LT_put(parent,TYPE_LIST,TAIL,&type_info->ltv),"linking child to parent in sequence");
                            if (name)
                                STRY(!LT_put(parent,name,TAIL,&type_info->ltv),"linking type info to parent");
                        }
                        break;
                    case DW_TAG_unspecified_parameters: // varargs
                        if (parent)
                            STRY(!LT_put(parent,"unspecified parameters",TAIL,&type_info->ltv),"linking unspecified parameters to parent");
                        break;
                    default:
                        if (parent && name)
                            STRY(!LT_put(parent,name,TAIL,&type_info->ltv),"linking type info to parent");
                        break;
                }
            done:
                return status;
            }

            char *name=NULL;
            Dwarf_Off offset;
            char offset_str[TYPE_IDLEN];
            STRY(dwarf_dieoffset(die,&offset,&error),"getting global die offset");
            DWARF_ID(offset_str,offset);

            if (!(type_info=(TYPE_INFO_LTV *) LT_get(index,offset_str,HEAD,KEEP))) { // may have been curated previously
                STRY(!(type_info=NEW(TYPE_INFO_LTV)),"creating a type_info item");
                type_info->depth=depth;
                LTV_init(&type_info->ltv,type_info,sizeof(TYPE_INFO_LTV),LT_BIN|LT_CVAR|LT_TYPE); // special derived LTV! LTV won't delete "itself" (i.e. data); LTV_release will delete the whole TYPE_INFO
                STRY(populate_type_info(dbg,die,type_info,&cu_data),"populating die type info");
                if ((name=get_diename(dbg,die))) // name is allocated from heap...
                    STRY(!attr_own(&type_info->ltv,TYPE_NAME,name),"naming type info");

                if (type_info->tag) {
                    DEBUG(fprintf(OUTFILE,"%*c",MAX(0,depth*4-2),' '));
                    DEBUG(print_type_info(OUTFILE,type_info));
                    DEBUG(fprintf(OUTFILE," [%s]\n",name));
                }
                else DEBUG(fprintf(OUTFILE,"disqualified die %s\n",type_info->id_str));

                if (type_info->tag!=DW_TAG_compile_unit || LT_get(module,name,HEAD,KEEP)) {
                    STRY(link2parent(name),"linking die to parent");
                    STRY(!LT_put(index,type_info->id_str,TAIL,&type_info->ltv),"indexing type info");
                    if (parent_type_info && (parent_type_info->tag==DW_TAG_type_unit)) {
                        static char alias[32];
                        DWARF_ALIAS(alias,cu_data.sig8);
                        STRY(!LT_put(aliases,alias,TAIL,&type_info->ltv),"aliasing type info %s with %s",type_info->id_str,alias);
                        DEBUG(fprintf(OUTFILE,"---aliasing type info %s with %s\n",type_info->id_str,alias));
                    }
                    STRY(traverse_child(dbg,die,child_op,flags|RDW_traverse_sibs),"traversing child and its siblings");

                    if (type_info->tag==DW_TAG_compile_unit)
                        read_cu_macros();
                } else
                    goto done; // don't traverse excluded CU's siblings

                if (type_info->flags&TYPEF_BASE) {
                    LTV *base=NULL;
                    if (!type_info->base) {
                        static char alias[32];
                        DWARF_ALIAS(alias,type_info->sig8);
                        base=resolve_alias(&type_info->ltv);
                    } else {
                        base=LT_get(index,type_info->base_str,HEAD,KEEP);
                        if (!base) { // may need to look ahead to resolve a base
                            Dwarf_Die basedie;
                            STRY(dwarf_offdie_b(dbg,type_info->base,(type_info->flags&TYPEF_IS_INFO)!=0,&basedie,&error),"looking up forward-referenced die");
                            STRY(curate_die(dbg,basedie,(flags&RDW_is_info)),"processing forward-referenced die (no sibs)");
                            base=LT_get(index,type_info->base_str,HEAD,KEEP); // grabbing it from index
                        }
                    }
                    if (base) // we can link base immediately
                        LT_put(&type_info->ltv,TYPE_BASE,HEAD,base);
                    else
                        DEBUG(fprintf(OUTFILE," >>>>  deferring base lookup for %s\n",type_info->id_str));
                }
            }
        done:
            return status;
        }

        return work_op(NULL,die,0);
    }

    void *resolve_aliases(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        if (!listree_acyclic(lti,ltvr,ltv,depth,flags)) {
            if ((*flags&LT_TRAVERSE_LTV) && (*ltv)->flags&LT_TYPE) {
                LTV *base=resolve_alias(*ltv);
                if (base)
                    LT_put((*ltv),TYPE_BASE,HEAD,base);
            }
        }
        return NULL;
    }

    void *remove_die_names(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        if (!listree_acyclic(lti,ltvr,ltv,depth,flags)) {
            if ((*flags&LT_TRAVERSE_LTV) && (*ltv)->flags&LT_TYPE)
                attr_del((*ltv),TYPE_NAME);
        }
        return NULL;
    }

    void *resolve_meta(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
            if ((*flags&LT_TRAVERSE_LTV) && (*ltv)->flags&LT_TYPE) {
                TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *)(*ltv);
                LTV *base_info=NULL;
                if (type_info->tag==DW_TAG_pointer_type && (base_info=LT_get(&type_info->ltv,TYPE_BASE,HEAD,KEEP)))
                    LT_put(base_info,TYPE_META,TAIL,&type_info->ltv);
        }
        return NULL;
    }

    STRY(traverse_cus(filename,curate_die,&cu_data,RDW_traverse_sibs),"traversing module type units");
    STRY(ltv_traverse(index,resolve_aliases,NULL)!=NULL,"resolving type_unit alias references"); // type_units can fwd-reference other type units
    LTV_release(index); // type_info indecies would conflict with debug_info indicoes
    index=aliases; // type_info sig8's are what carry forwards into debug_info section
    STRY(traverse_cus(filename,curate_die,&cu_data,RDW_traverse_sibs|RDW_is_info),"traversing module compute units");
    resolve_symbols();
    LTV_release(index); // i.e. aliases
    STRY(ltv_traverse(module,remove_die_names,resolve_meta)!=NULL,"cleaning up and linking types to pointers"); // link X.meta to pointer-to-X
    cif_ffi_prep(module);

 done:
    return status;
}
int cif_curate_module(LTV *module,int bootstrap) { return _cif_curate_module(module,bootstrap); }



char *Type_pushUVAL(TYPE_UVALUE *uval,char *buf)
{
    switch(uval->base.dutype) {
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
        case TYPE_FLOAT16: sprintf(buf,"%Lg",   uval->float16.val); break;
        case TYPE_ADDR:    sprintf(buf,"%p",    uval->addr.val);    break;
        default:           buf[0]=0; break;
    }
    return buf;
}

TYPE_UVALUE *Type_pullUVAL(TYPE_UVALUE *uval,char *buf)
{
    int tVar;
    switch(uval->base.dutype) {
        case TYPE_INT1S:   sscanf(buf,"%i",  &tVar);uval->int1s.val=tVar; break;
        case TYPE_INT2S:   sscanf(buf,"%i",  &tVar);uval->int2s.val=tVar; break;
        case TYPE_INT4S:   sscanf(buf,"%i",  &tVar);uval->int4s.val=tVar; break;
        case TYPE_INT8S:   sscanf(buf,"%lli",&uval->int8s.val);           break;
        case TYPE_INT1U:   sscanf(buf,"%u",  &tVar);uval->int1u.val=tVar; break;
        case TYPE_INT2U:   sscanf(buf,"%u",  &tVar);uval->int2u.val=tVar; break;
        case TYPE_INT4U:   sscanf(buf,"%u",  &tVar);uval->int4u.val=tVar; break;
        case TYPE_INT8U:   sscanf(buf,"%llu",&uval->int8u.val);           break;
        case TYPE_FLOAT4:  sscanf(buf,"%g",  &uval->float4.val);          break;
        case TYPE_FLOAT8:  sscanf(buf,"%g",  &uval->float8.val);          break;
        case TYPE_FLOAT16: sscanf(buf,"%Lg", &uval->float16.val);         break;
        case TYPE_ADDR:    sscanf(buf,"%p",  &uval->addr.val);            break;
        default:           buf[0]=0; break;
    }
    return uval;
}


#define UVAL2VAR(uval,var)                                                      \
    do {                                                                        \
        switch (uval.dutype) {                                                  \
            case TYPE_INT1S:   var=(typeof(var)) uval.int1s.val;   break;       \
            case TYPE_INT2S:   var=(typeof(var)) uval.int2s.val;   break;       \
            case TYPE_INT4S:   var=(typeof(var)) uval.int4s.val;   break;       \
            case TYPE_INT8S:   var=(typeof(var)) uval.int8s.val;   break;       \
            case TYPE_INT1U:   var=(typeof(var)) uval.int1u.val;   break;       \
            case TYPE_INT2U:   var=(typeof(var)) uval.int2u.val;   break;       \
            case TYPE_INT4U:   var=(typeof(var)) uval.int4u.val;   break;       \
            case TYPE_INT8U:   var=(typeof(var)) uval.int8u.val;   break;       \
            case TYPE_FLOAT4:  var=(typeof(var)) uval.float4.val;  break;       \
            case TYPE_FLOAT8:  var=(typeof(var)) uval.float8.val;  break;       \
            case TYPE_FLOAT16: var=(typeof(var)) uval.float16.val; break;       \
            case TYPE_ADDR:    var=(typeof(var)) uval.addr.val;    break;       \
        }                                                                       \
    } while(0)

#define GETUVAL(member,type,uval)                                       \
    do {                                                                \
        uval->member.dutype = type;                                     \
        uval->member.val = *(typeof(uval->member.val) *) cvar->data;    \
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
    TRYCATCH(!(type=cif_find_concrete(LT_get(cvar,TYPE_BASE,HEAD,KEEP))),0,done,"retrieving cvar basic type");
    TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type->data;

    ull size=type_info->bytesize;
    ull bitsize=type_info->bitsize;
    ull bitoffset=type_info->bitoffset;
    ull encoding;
    switch (type_info->tag) {
        case DW_TAG_member:           encoding=DW_ATE_signed;       break; // bitfield
        case DW_TAG_enumeration_type: encoding=DW_ATE_signed;       break;
        case DW_TAG_pointer_type:     encoding=DW_ATE_unsigned;     break;
        case DW_TAG_base_type:        encoding=type_info->encoding; break;
        default: goto done;
    }

    BZERO(*uval);
    switch (encoding) {
        case DW_ATE_float:
            if      (size==4)  GETUVAL(float4,TYPE_FLOAT4,uval);
            else if (size==8)  GETUVAL(float8,TYPE_FLOAT8,uval);
            else if (size==16) GETUVAL(float16,TYPE_FLOAT16,uval);
            break;
        case DW_ATE_signed:
        case DW_ATE_signed_char:
            if      (size==1)  GETUBITS(int1s,TYPE_INT1S,uval,bitsize,bitoffset,1);
            else if (size==2)  GETUBITS(int2s,TYPE_INT2S,uval,bitsize,bitoffset,1);
            else if (size==4)  GETUBITS(int4s,TYPE_INT4S,uval,bitsize,bitoffset,1);
            else if (size==8)  GETUBITS(int8s,TYPE_INT8S,uval,bitsize,bitoffset,1);
            break;
        case DW_ATE_boolean:
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
    return status?0:uval->base.dutype;
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
    STRY(!(type=cif_find_concrete(LT_get(cvar,TYPE_BASE,HEAD,KEEP))),"retrieving cvar basic type");
    TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type->data;

    ull size=type_info->bytesize;
    ull bitsize=type_info->bitsize;
    ull bitoffset=type_info->bitoffset;
    ull encoding;
    switch (type_info->tag) {
        case DW_TAG_member:           encoding=DW_ATE_signed;       break; // bitfield
        case DW_TAG_enumeration_type: encoding=DW_ATE_signed;       break;
        case DW_TAG_pointer_type:     encoding=DW_ATE_unsigned;     break;
        case DW_TAG_base_type:        encoding=type_info->encoding; break;
        default: goto done;
    }

    switch (encoding) {
        case DW_ATE_float:
            if      (size==4)  PUTUVAL(float4, TYPE_FLOAT4, uval);
            else if (size==8)  PUTUVAL(float8, TYPE_FLOAT8, uval);
            else if (size==16) PUTUVAL(float16,TYPE_FLOAT16,uval);
            break;
        case DW_ATE_signed:
        case DW_ATE_signed_char:
            if      (size==1)  PUTUBITS(int1s,TYPE_INT1S,uval,bitsize,bitoffset,1);
            else if (size==2)  PUTUBITS(int2s,TYPE_INT2S,uval,bitsize,bitoffset,1);
            else if (size==4)  PUTUBITS(int4s,TYPE_INT4S,uval,bitsize,bitoffset,1);
            else if (size==8)  PUTUBITS(int8s,TYPE_INT8S,uval,bitsize,bitoffset,1);
            break;
        case DW_ATE_boolean:
        case DW_ATE_unsigned:
        case DW_ATE_unsigned_char:
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


int Type_isBitField(TYPE_INFO_LTV *type_info) { return (type_info->bitsize || type_info->bitoffset); }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LTV *basic_ffi_ltv(LTV *type)
{
    int status=0;
    static ffi_type *ffi_type_array[] = { &ffi_type_void, &ffi_type_pointer, &ffi_type_float, &ffi_type_double, &ffi_type_longdouble, &ffi_type_sint8, &ffi_type_sint16, &ffi_type_sint32, &ffi_type_sint64, &ffi_type_uint8, &ffi_type_uint16, &ffi_type_uint32, &ffi_type_uint64, NULL};
    enum                                { FT_VOID,        FT_POINTER,        FT_FLOAT,        FT_DOUBLE,        FT_LONGDOUBLE,        FT_SINT8,        FT_SINT16,        FT_SINT32,        FT_SINT64,        FT_UINT8,        FT_UINT16,        FT_UINT32,        FT_UINT64,        FT_MAX };

    static CLL ffi_type_ltv_preserver;
    static LTV *ffi_type_ltv[FT_MAX];
    static int initialized=false;
    if (!initialized) {
        CLL_init(&ffi_type_ltv_preserver);
        for (int i=0;i<FT_MAX;i++) {
            ffi_type_ltv[i]=LTV_init(NEW(LTV),ffi_type_array[i],sizeof(ffi_type),LT_BIN|LT_CVAR|LT_FFI);
            LTV_enq(&ffi_type_ltv_preserver,ffi_type_ltv[i],TAIL);
        }
        initialized=true;
    }

    LTV *rval=NULL;

    if (!type) { // total hack way to get static ffi_type_void LTV
        rval=ffi_type_ltv[FT_VOID];
        goto done;
    }

    LTV *basic_type=NULL;
    TRYCATCH(!(basic_type=cif_find_concrete(type)),0,done,"resolving basic type");
    TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) basic_type->data;

    ull size=type_info->bytesize;
    ull bitsize=type_info->bitsize;
    ull bitoffset=type_info->bitoffset;
    ull encoding;
    switch (type_info->tag) {
        case DW_TAG_pointer_type:
        case DW_TAG_array_type:
            rval=ffi_type_ltv[FT_POINTER];
            goto done;
        case DW_TAG_enumeration_type:
        case DW_TAG_enumerator:
            encoding=DW_ATE_signed;
            break;
        case DW_TAG_member:
            if (bitsize) // only for bitfields
                encoding=DW_ATE_unsigned; // TODO: DW_AT_type actually propagates through to sint or uint!!!
            break;
        case DW_TAG_base_type:
            encoding=type_info->encoding;
            break;
        default: goto done;
    }

    switch (encoding) {
        case DW_ATE_float:
            if      (size==4)  rval=ffi_type_ltv[FT_FLOAT];
            else if (size==8)  rval=ffi_type_ltv[FT_DOUBLE];
            else if (size==16) rval=ffi_type_ltv[FT_LONGDOUBLE];
            break;
        case DW_ATE_signed:
        case DW_ATE_signed_char:
            if      (size==1)  rval=ffi_type_ltv[FT_SINT8];
            else if (size==2)  rval=ffi_type_ltv[FT_SINT16];
            else if (size==4)  rval=ffi_type_ltv[FT_SINT32];
            else if (size==8)  rval=ffi_type_ltv[FT_SINT64];
            break;
        case DW_ATE_boolean:
        case DW_ATE_unsigned:
        case DW_ATE_unsigned_char:
            if      (size==1)  rval=ffi_type_ltv[FT_UINT8];
            else if (size==2)  rval=ffi_type_ltv[FT_UINT16];
            else if (size==4)  rval=ffi_type_ltv[FT_UINT32];
            else if (size==8)  rval=ffi_type_ltv[FT_UINT64];
            break;
        default:
            break;
    }
 done:
    return rval;
}


LTV *cvar_ffi_ltv(LTV *type,int *size)
{
    int status=0;
    *size=0;
    LTV *ffi_type_ltv=NULL;
    TRYCATCH(!(type=cif_find_concrete(type)),0,done,"resolving basic type");
    *size=((TYPE_INFO_LTV *) type->data)->bytesize;
    ffi_type_ltv=LT_get(type,FFI_TYPE,HEAD,KEEP);
 done:
    return ffi_type_ltv;
}

// prepare a type_info cvar for ffi use
int cif_ffi_prep(LTV *type)
{
    int status=0;

    int collate_child_ffi_types(LTV *ltv,int tag,int *count,ffi_type ***child_types)
    {
        int status=0;
        int largest=0;
        char *name=attr_get(ltv,TYPE_SYMB);
        DEBUG(fprintf(OUTFILE,"ffi_prep child for %s\n",name));
        LTI *children=LTI_resolve(ltv,TYPE_LIST,false);
        if (tag==DW_TAG_union_type)
            *count=1;
        else
            *count=children?CLL_len(&children->ltvs):0;
        (*child_types)=calloc(sizeof(ffi_type *),(*count)+1);
        if (*count) {
            int size=0,largest=0,index=0;
            unsigned lastloc=0xffffffff;
            void *get_child_ffi_type(CLL *lnk) {
                int status=0;
                LTV *child_type=((LTVR *) lnk)->ltv;
                char *child_name=attr_get(child_type,TYPE_SYMB);
                TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) child_type->data;
                DEBUG(fprintf(OUTFILE,"ffi_prep child %s(%s)\n",child_name,type_info->id_str));
                LTV *child_ffi_ltv=cvar_ffi_ltv(child_type,&size);
                STRY(!child_ffi_ltv,"validating child ffi ltv");

                if (tag==DW_TAG_union_type) {
                    if (largest<size) {
                        largest=size;
                        (*child_types)[index]=(ffi_type *) child_ffi_ltv->data;
                    }
                } else if ((type_info->tag==DW_TAG_member) && (type_info->data_member_location==lastloc)) { // overlapping bitfields in a structure
                    (*count)--;
                    goto done;
                }
                else
                    (*child_types)[index++]=(ffi_type *) child_ffi_ltv->data;

            done:
                lastloc=type_info->data_member_location;
                return status?NON_NULL:NULL;
            }
            CLL_map(&children->ltvs,FWD,get_child_ffi_type);
        }
        (*child_types)[*count]=NULL; // null terminate the array for the struct case

    done:
        return status;
    }

    void *pre(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        if (((*flags)&LT_TRAVERSE_LTV)) {
            if ((*ltv)->flags&LT_RVIS) // allow absolute descent but not recursive descent
                (*flags)|=LT_TRAVERSE_HALT;
            else {
                char *name=attr_get((*ltv),TYPE_SYMB);
                if ((*ltv)->flags&LT_TYPE) {
                    TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) (*ltv);
                    DEBUG(fprintf(OUTFILE,"ffi_prep pre %s(%s)\n",name,type_info->id_str));
                    int size=0;
                    switch (type_info->tag) {
                        case DW_TAG_union_type:
                        case DW_TAG_structure_type:
                        case DW_TAG_subprogram:
                        case DW_TAG_subroutine_type:
                            if (LT_get((*ltv),FFI_TYPE,HEAD,KEEP)) // definitely have traversed this subtree
                                *flags|=LT_TRAVERSE_HALT;
                            break; // complex types, handle in post
                        default: { // fill in basic types in pre, halting traversal for each
                            LTV *basic_type=cif_find_concrete(*ltv);
                            if (basic_type && !LT_get(basic_type,FFI_TYPE,HEAD,KEEP)) {
                                LTV *ftl=basic_ffi_ltv(basic_type); // only returns something for basic types
                                if (ftl) { // basic types
                                    STRY(!LT_put(basic_type,FFI_TYPE,HEAD,ftl),"installing basic ffi type");
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }

    done:
        return status?NON_NULL:NULL;
    }

    void *post(LTI **lti,LTVR *ltvr,LTV **ltv,int depth,LT_TRAVERSE_FLAGS *flags) {
        if ((*flags&LT_TRAVERSE_LTV)) {
            char *name=attr_get((*ltv),TYPE_SYMB);
            if ((*ltv)->flags&LT_TYPE) {
                TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) (*ltv);
                DEBUG(fprintf(OUTFILE,"ffi_prep post %s(%s)\n",name,type_info->id_str));
                if (!LT_get((*ltv),FFI_TYPE,HEAD,KEEP)) {
                    switch (type_info->tag) {
                        case DW_TAG_union_type:
                        case DW_TAG_structure_type: { // complex
                            LTV *ffi_type_ltv=LTV_init(NEW(LTV),NEW(ffi_type),sizeof(ffi_type),LT_OWN|LT_BIN|LT_CVAR|LT_FFI);
                            LT_put((*ltv),FFI_TYPE,HEAD,ffi_type_ltv);
                            ffi_type *ft=(ffi_type *) ffi_type_ltv->data;
                            int count=0;
                            ft->type=FFI_TYPE_STRUCT;
                            STRY(collate_child_ffi_types((*ltv),type_info->tag,&count,&ft->elements),"collating %s child ffi types",name);
                            break;
                        }
                        case DW_TAG_subprogram:
                        case DW_TAG_subroutine_type: { // complex
                            int size;
                            LTV *return_type=cvar_ffi_ltv((*ltv),&size);
                            if (!return_type)
                                return_type=basic_ffi_ltv(NULL); // hacky way to get the static ffi_type_void LTV
                            LT_put((*ltv),FFI_TYPE,HEAD,return_type);
                            int argc=0;
                            ffi_type **argv=NULL;
                            STRY(collate_child_ffi_types((*ltv),type_info->tag,&argc,&argv),"collating subprogram child ffi types");

                            LTV *cif_ltv=LTV_init(NEW(LTV),NEW(ffi_cif),sizeof(ffi_cif),LT_OWN|LT_BIN|LT_CVAR|LT_CIF);
                            LT_put((*ltv),FFI_CIF,HEAD,cif_ltv);
                            STRY(ffi_prep_cif((ffi_cif *) cif_ltv->data,FFI_DEFAULT_ABI,argc,(ffi_type *) return_type->data,argv),"prepping cif");
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }

    done:
        return status?NON_NULL:NULL;
    }

    // postfix traversal so structs/unions are processed after their members. also need to catch circular dependencies
    STRY(ltv_traverse(type,pre,post)!=NULL,"traversing module");
 done:
    return status;
}


LTV *cif_rval_create(LTV *lambda,void *data)
{
    // assumes lambda is a CVAR/TYPE_INFO/subprogram
    // error check later...
    LTV *ltv=NULL;
    LTV *cvar_type=LT_get(lambda,TYPE_BASE,HEAD,KEEP);
    if (cvar_type) {
        LTV *return_type=cif_find_concrete(cvar_type); // base type of a subprogram is its return type
        ltv=cif_create_cvar(return_type,data,NULL);
    }
    return ltv;
}

int cif_args_marshal(LTV *lambda,int dir,int (*marshal)(char *argname,LTV *type))
{
    int status=0;
    void *marshal_arg(CLL *lnk) {
        int status=0;
        LTV *arg=((LTVR *) lnk)->ltv;
        LTV *arg_type=LT_get(arg,FFI_TYPE,HEAD,KEEP);
        char *name=attr_get(arg_type,TYPE_SYMB);
        LTV *type=cif_find_symbolic(((LTVR *) lnk)->ltv);
        STRY(marshal(name,type),"retrieving ffi arg from environment");
    done:
        return status?NON_NULL:NULL;
    }
    LTI *children=NULL;
    TRYCATCH(!(children=LTI_resolve(lambda,TYPE_LIST,false)),0,done,"retrieving ffi args");;
    STRY(CLL_map(&children->ltvs,dir,marshal_arg)!=NULL,"marshalling ffi args");
 done:
    return status;
}

LTV *cif_isaddr(LTV *cvar)
{
    int status=0;
    LTV *type=NULL;
    TYPE_INFO_LTV *type_info=NULL;
    STRY(!cvar,"validating params");
    if (type=cif_find_concrete(LT_get(cvar,TYPE_BASE,HEAD,KEEP)))
        type_info=(TYPE_INFO_LTV *) type->data;
 done:
    return (status|!type)?NULL:type_info->tag!=DW_TAG_pointer_type?NULL:type;
}

int cif_iszero(LTV *cvar)
{
    TYPE_UVALUE uval={};
    switch (Type_getUVAL(cvar,&uval)) {
        case TYPE_INT1S:   return uval.int1s.val == 0;
        case TYPE_INT2S:   return uval.int2s.val == 0;
        case TYPE_INT4S:   return uval.int4s.val == 0;
        case TYPE_INT8S:   return uval.int8s.val == 0;
        case TYPE_INT1U:   return uval.int1u.val == 0;
        case TYPE_INT2U:   return uval.int2u.val == 0;
        case TYPE_INT4U:   return uval.int4u.val == 0;
        case TYPE_INT8U:   return uval.int8u.val == 0;
        case TYPE_FLOAT4:  return uval.float4.val == 0;
        case TYPE_FLOAT8:  return uval.float8.val == 0;
        case TYPE_FLOAT16: return uval.float16.val == 0;
        case TYPE_ADDR:    return uval.addr.val == NULL;
        default: return -1;
    }
}

int cif_ispos(LTV *cvar)
{
    TYPE_UVALUE uval={};
    switch (Type_getUVAL(cvar,&uval)) {
        case TYPE_INT1S:   return uval.int1s.val > 0;
        case TYPE_INT2S:   return uval.int2s.val > 0;
        case TYPE_INT4S:   return uval.int4s.val > 0;
        case TYPE_INT8S:   return uval.int8s.val > 0;
        case TYPE_INT1U:   return 1;
        case TYPE_INT2U:   return 1;
        case TYPE_INT4U:   return 1;
        case TYPE_INT8U:   return 1;
        case TYPE_FLOAT4:  return uval.float4.val > 0;
        case TYPE_FLOAT8:  return uval.float8.val > 0;
        case TYPE_FLOAT16: return uval.float16.val > 0;
        case TYPE_ADDR:    return uval.addr.val > (void *) 0;
        default: return -1;
    }
}

int cif_isneg(LTV *cvar)
{
    TYPE_UVALUE uval={};
    switch (Type_getUVAL(cvar,&uval)) {
        case TYPE_INT1S:   return uval.int1s.val < 0;
        case TYPE_INT2S:   return uval.int2s.val < 0;
        case TYPE_INT4S:   return uval.int4s.val < 0;
        case TYPE_INT8S:   return uval.int8s.val < 0;
        case TYPE_INT1U:   return 0;
        case TYPE_INT2U:   return 0;
        case TYPE_INT4U:   return 0;
        case TYPE_INT8U:   return 0;
        case TYPE_FLOAT4:  return uval.float4.val < 0;
        case TYPE_FLOAT8:  return uval.float8.val < 0;
        case TYPE_FLOAT16: return uval.float16.val < 0;
        case TYPE_ADDR:    return uval.addr.val < (void *) 0;
        default: return -1;
    }
}

extern LTV *cif_get_meta(LTV *ltv) { return LT_get(ltv,TYPE_META,HEAD,KEEP); }

extern LTV *cif_put_meta(LTV *ltv,LTV *meta) {
    LTV *result=ltv;
    if (ltv && meta) {
        result=cif_create_cvar(meta,&ltv->data,NULL);
        LT_put(result,TYPE_CAST,HEAD,ltv);
    }
    return result;
}

/*
extern LTV *cif_box(LTV *ltv) {
    LTV *result=ltv;
    LTV *meta=cif_get_meta(cvar_base);
    if (meta && type_base==meta) // allow X to X* coersions
        result=cif_put_meta(ltv,meta);
}
*/

int is_readable(LTV *type)
{
    if (type && type->flags&LT_TYPE) {
        TYPE_INFO_LTV *type_info=(TYPE_INFO_LTV *) type;
        switch(type_info->tag) {
            case DW_TAG_enumeration_type:
                //case DW_TAG_pointer_type:
            case DW_TAG_base_type:
                return true;
            default:
                break;
        }
    }
    return false;
}

// convert interpreter params into something FFI can use
extern void print_type(LTV *ltv,char *prefix)
{
    int old_show_ref=show_ref;
    show_ref=1;
    if (prefix)
        fprintf(OUTFILE,"%s",prefix);
    print_ltv(OUTFILE,NULL,ltv,NULL,2);
    show_ref=old_show_ref;
}

// (encaps LTVs into LTV cvars, cast basic types, ref/deref pointers, ...)
LTV *cif_coerce_i2c(LTV *ltv,LTV *type)
{
    int status=0;
    LTV *result=ltv;

    /*
    int old_show_ref=show_ref;
    show_ref=1;
    fprintf(OUTFILE,"coercing ltv\n");
    print_ltv(OUTFILE,NULL,cvar,NULL,2);
    fprintf(OUTFILE,"into type\n");
    print_ltv(OUTFILE,NULL,type,NULL,2);
    show_ref=old_show_ref;
    */

    LTV *type_base=cif_find_concrete(type);

    char *type_name=attr_get(type_base,TYPE_SYMB);
    int match(char *key) { return !strcmp(key,type_name); }
    if (!(ltv->flags&LT_CVAR)) { // first, dress a non-cvar ltv up in something appropriate
        if (match("(LTV)*"))
            result=cif_create_cvar(cif_type_info("LTV"),ltv,NULL); // encaps LTV when dest is an LTV*
        else if (match("(char)*") || match("(unsigned char)*")) // ltv data -> char array
            STRY(!(result=cif_create_cvar(type_base,&ltv->data,NULL)),"creating string coersion"); // cvar->data=&ltv->data, i.e. cvar will point to a void*
        else if (is_readable(type_base)) {
            TYPE_UVALUE dst_uval={},src_uval={};
            result=cif_create_cvar(type,NULL,NULL);
            Type_getUVAL(result,&dst_uval); // try to read plaintext into a base type
            Type_putUVAL(result,Type_pullUVAL(&dst_uval,ltv->data));
        } else {
            STRY((vm_throw(LTV_NULL),1),"no i2c coersion");
        }
        STRY(!(LT_put(result,TYPE_CAST,HEAD,ltv)),"linking original to coersion");
        ltv=result; // may need further coersions
    }

    LTV *cvar_base=LT_get(ltv,TYPE_BASE,HEAD,KEEP);
    if (type_base && cvar_base) {
        if (type_base==cvar_base)
            result=ltv;
        else if (basic_ffi_ltv(type_base)==basic_ffi_ltv(cvar_base))
            result=ltv;
        else {
            LTV *meta=cif_get_meta(cvar_base);
            if (meta && type_base==meta) // allow X to X* coersions
                result=cif_put_meta(ltv,meta);
            else if (match("(LTV)*"))
                result=encaps_ltv(ltv);
            else
                STRY((vm_throw(LTV_NULL),1),"no i2c coersion");
        }
    }
 done:
    return status?NULL:result;
}

// coerce C return value into interpreter-friendly LTV
// (mostly unwrapping LTV cvars)
LTV *cif_coerce_c2i(LTV *ltv)
{
    int status=0;
    LTV *result=ltv;

    /*
    int old_show_ref=show_ref;
    show_ref=1;
    fprintf(OUTFILE,"coercing ltv\n");
    print_ltv(OUTFILE,NULL,cvar,NULL,2);
    fprintf(OUTFILE,"into type\n");
    print_ltv(OUTFILE,NULL,type,NULL,2);
    show_ref=old_show_ref;
    */

    LTV *type=LT_get(ltv,TYPE_BASE,HEAD,KEEP);
    char *type_name=attr_get(type,TYPE_SYMB);
    if (type_name && !strcmp(type_name,"(LTV)*")) {
        result=*(LTV **) ltv->data;
        LTV_release(ltv);
    }

 done:
    return status?NULL:result;
}

int cif_ffi_call(LTV *type,void *loc,LTV *rval,CLL *coerced_args)
{
    int status=0;
    int index=0;
    void **args=NULL;
    LTV *cif_ltv=LT_get(type,FFI_CIF,HEAD,KEEP);
    int arity=CLL_len(coerced_args);
    args=calloc(sizeof(void *),arity);

    void *index_arg(CLL *lnk) {
        LTV *ltv=((LTVR *) lnk)->ltv;
        if  (ltv->flags&LT_ARR)
            args[index++]=&ltv->data;
        else
            args[index++]=ltv->data;
        return NULL;
    }
    CLL_map(coerced_args,FWD,index_arg);

    ffi_call((ffi_cif *) cif_ltv->data,loc,rval->data,args); // no return value

 done:
    return status;
}

LTV *cif_type_info(char *typename) { return LT_get(cif_module,typename,HEAD,KEEP); }



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CLOSURE API
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LTV *cif_create_closure(LTV *function_type,void (*thunk) (ffi_cif *CIF, void *RET, void**ARGS, void *USER_DATA))
{
    int status=0;
    LTV *closure=NULL;

    LTV *function_ltv=NULL,*ffi_cif_ltv=NULL;
    STRY(!(function_ltv=cif_find_function(function_type)),"finding closure type");
    STRY(!(ffi_cif_ltv=LT_get(function_ltv,FFI_CIF,HEAD,KEEP)),"getting closure cif");
    ffi_cif *cif=(ffi_cif *) ffi_cif_ltv->data;
    void *executable=NULL;
    void *writeable=ffi_closure_alloc(sizeof(ffi_closure),&executable);
    STRY(!(closure=cif_create_cvar(function_ltv,NULL,NULL)),"creating closure cvar");
    STRY(ffi_prep_closure_loc(writeable,cif,thunk,closure,executable)!=FFI_OK,"prepping closure"); // closure passes itself to callback
    LTV_renew(closure,executable,0,LT_NONE); // update closure with function pointer
    LT_put(closure,"WRITEABLE", HEAD,LTV_init(NEW(LTV),writeable, 0,LT_NONE));

 done:
    return closure;
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

int Type_traverseArray(DICT *dict,void *data,TYPE_INFO_LTV *type_info,Type_traverseFn fn)
{
    TYPE_INFO_LTV subrange_info,element_info;
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

int Type_traverseUnion(DICT *dict,void *data,TYPE_INFO_LTV *type_info,Type_traverseFn fn)
{
    TYPE_INFO_LTV local_type_info;
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


char *Type_humanReadableVal(TYPE_INFO_LTV *type_info,char *buf)
{
    strcpy(buf,"n/a");

    TYPE_UVALUE uval={};
    if (!strcmp(type_info->category,"base_type"))
    {
        char buf2[64];
        if (Type_getUVAL(type_info,type_info->addr,&uval))
            sprintf(buf,"%s",Type_pushUVAL(&uval,buf2));
    } else if (!strcmp(type_info->category,"enumeration_type")) {
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




JLI_EXTENSION(reflect_czero)
{
    DICT_ITEM *var=jli_getitem(dict,"",0,1);
    if (var)
    {
        TYPE_INFO_LTV type_info;
        ull *loc=INTVAR(&var->dict,"addr",0);
        char *type=strvar(&var->dict,"type",0);
        DICT_ITEM *typeitem=Type_findBasic(dict,Type_lookupName(dict,type),&ZERO(type_info));
        ull *byte_size=STRTOULLP(type_info.DW_AT_byte_size);
        if (byte_size && type && loc) bzero((char *) *loc,*byte_size);
    }
    DELETE(dict_free(var));
    return 0;
}



char *reflect_enumstr(char *type,unsigned int value)
{
    TYPE_INFO_LTV type_info;
    DICT_ITEM *enumitem,*typeitem;
    char *valstr=ulltostr("values.%llu",(ull)value);

    return ((typeitem=Type_findBasic(reflection_dict,Type_lookupName(reflection_dict,type),&type_info)) &&
            (enumitem=jli_getitem(&typeitem->dict,valstr,strlen(valstr),0)))?
        enumitem->data:NULL;
}


#endif
