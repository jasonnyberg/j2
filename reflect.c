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


#include "edict.h"


LTV *reflection_member(LTV *val,char *name); // dereference member by name

int reflection_dump(LTv *val); // dump binary data, metadata to stdout

char *refelction_write(LTV *val); // to_string(s)
int reflection_read(LTV *val,char *value); // from_string(s)

int reflection_new(char *type); // expose through edict
int reflection_delete(LTV *val); // expose through edict

int reflection_pickle(); // TOS cvar to edict representation
int reflection_unpickle(); // TOS edict representation to cvar



LTV *reflection_member(LTV *val,char *name)
{
    return NULL;
}



#define _GNU_SOURCE // strndupa, stpcpy
#define __USE_GNU // strndupa, stpcpy
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"
#include "hdict.h"
#include "jli.h"
#include "jliext.h"
#include "reflect.h"

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
                case 4: // float
                    if (*size==4)  GETUVAL(float4,TYPE_FLOAT4,uval);
                    if (*size==8)  GETUVAL(float8,TYPE_FLOAT8,uval);
                    if (*size==12) GETUVAL(float12,TYPE_FLOAT12,uval);
                    break;
                case 5: // signed int
                case 6: // signed char
                    if (*size==1) GETUBITS(int1s,TYPE_INT1S,uval,bit_size,bit_offset,1);
                    if (*size==2) GETUBITS(int2s,TYPE_INT2S,uval,bit_size,bit_offset,1);
                    if (*size==4) GETUBITS(int4s,TYPE_INT4S,uval,bit_size,bit_offset,1);
                    if (*size==8) GETUBITS(int8s,TYPE_INT8S,uval,bit_size,bit_offset,1);
                    break;
                case 7: // unsigned int
                case 8: // unsigned char
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

void reflect(char *command)
{
    /*sem-protect:*/
    //printf(CODE_RED "%s\n" CODE_RESET,command);
    
    {
        if (command)
            jli_parse(reflection_dict,command);
        else
            jli_read(reflection_dict,reflection_fifoname,jli_interact,1);
    }
}
