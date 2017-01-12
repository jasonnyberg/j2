/*
  Copyright (c) 2009, David Anderson.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the example nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY David Anderson ''AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL David Anderson BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
/*
  Copyright (c) 2011, Jason Nyberg.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the example nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY Jason Nyberg ''AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL David Anderson BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/* myreader.c
   This is an example of code reading dwarf .debug_info.
   It is kept as simple as possible to expose essential features.
   It does not do all possible error reporting or error handling.

   To use, try
       make
       ./myreader myreader
*/

#include <sys/types.h> /* For open() */
#include <sys/stat.h>  /* For open() */
#include <fcntl.h>     /* For open() */
#include <stdlib.h>     /* For exit() */
#include <unistd.h>     /* For close() */
#include <stdio.h>
#include <errno.h>
#include "dwarf.h"
#include "libdwarf.h"
#include "util.h"

#include "reflect.h"

#define PANIC(cond) TRYCATCH(cond!=DW_DLV_OK,-1,panic,#cond)
#define SKIP(cond,followup) if (cond==DW_DLV_OK) followup

void get_die_data(Dwarf_Debug dbg,Dwarf_Die die,TYPE_INFO *type_info)
{
    int status=0;
    char *diename = NULL,*name=NULL;
    Dwarf_Error error = 0;
    const char *tagname = 0;
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
    const char *vcstr;
    char *vstr;

    int res;

    PANIC(dwarf_dieoffset(die,&type_info->id,&error));
    PANIC(dwarf_tag(die,&type_info->tag,&error)); // DW_TAG_compile_unit subprogram formal_parameter enumeration_type enumerator variable structure_type base_type member typedef:

    TRYCATCH((res=dwarf_diename(die,&diename,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_diename");
    name=diename?diename:"__anon__";

    type_info->name=bufdup(name,-1);
    dwarf_dealloc(dbg,diename,DW_DLA_STRING);

    PANIC(dwarf_get_TAG_name(type_info->tag,&tagname));
    printf(CODE_RED "0x%" DW_PR_DUx " %s %s" CODE_RESET "\n",(int) type_info->id,tagname+7,name);

    SKIP(dwarf_bytesize  (die,&type_info->bytesize,  &error),printf("%"   DW_PR_DUu " bytesize\n",  type_info->bytesize));
    SKIP(dwarf_bitsize   (die,&type_info->bitsize,   &error),printf("%"   DW_PR_DUu " bitsize\n",   type_info->bitsize));
    SKIP(dwarf_bitoffset (die,&type_info->bitoffset, &error),printf("%"   DW_PR_DUu " bitoffset\n", type_info->bitoffset));
    SKIP(dwarf_srclang   (die,&type_info->srclang,   &error),printf("%"   DW_PR_DUu " srclang\n",   type_info->srclang));
    SKIP(dwarf_arrayorder(die,&type_info->arrayorder,&error),printf("%"   DW_PR_DUu " arrayorder\n",type_info->arrayorder));

    PANIC(dwarf_attrlist(die,&atlist,&atcnt,&error));
    Dwarf_Attribute *attr=NULL;
    while (--atcnt) {
        attr=&atlist[atcnt];

        PANIC(dwarf_whatattr(*attr,&vshort,&error));
        switch (vshort) {
            case DW_AT_data_member_location:
            case DW_AT_const_value:
                break;
            default:
                break;
                //return;
        }

        PANIC(dwarf_whatattr(*attr,&vshort,&error));
        PANIC(dwarf_get_AT_name(vshort,&vcstr));
        printf("    attr %d (%s) ",vshort,vcstr);

        /*
        PANIC(dwarf_whatform(*attr,&vshort,&error));
        PANIC(dwarf_get_FORM_name(vshort,&vcstr));
        printf("form %d (%s) ",vshort,vcstr);

        PANIC(dwarf_whatform_direct(*attr,&vshort,&error));
        PANIC(dwarf_get_FORM_name(vshort,&vcstr));
        printf("form_direct %d (%s) ",vshort,vcstr);
        */

        //SKIP(dwarf_formref(*attr,&voffset,&error),       printf("formref 0x%"        DW_PR_DSx " ",voffset));
        SKIP(dwarf_global_formref(*attr,&voffset,&error), printf("global_formref 0x%" DW_PR_DSx " ",voffset));
        SKIP(dwarf_formaddr(*attr,&vaddr,&error),         printf("addr 0x%"      DW_PR_DUx " ",vaddr));
        SKIP(dwarf_formflag(*attr,&vbool,&error),         printf("flag %"        DW_PR_DSd " ",vbool));
        SKIP(dwarf_formudata(*attr,&vuint,&error),        printf("udata %"       DW_PR_DUu " ",vuint));
        SKIP(dwarf_formsdata(*attr,&vint,&error),         printf("sdata %"       DW_PR_DSd " ",vint));
        SKIP(dwarf_formblock(*attr,&vblock,&error),       printf("block 0x%"     DW_PR_DUx " ",vblock->bl_len));
        SKIP(dwarf_formexprloc(*attr,&vuint,&vptr,&error),printf("exprloc len %" DW_PR_DUu " loc 0x%" DW_PR_DUx " ",vuint,vptr));
        SKIP(dwarf_formstring(*attr,&vstr,&error),        printf("string %s ",                  vstr));
        printf("\n");

        Dwarf_Locdesc **llbuf;
        if (dwarf_loclist_n(*attr,&llbuf,&vint,&error) == DW_DLV_OK)
        {
            int i,j;
            for (i=0;i<vint;i++)
            {
                printf("loclist %d ",i);
                printf("lowpc %" DW_PR_DUx " ",llbuf[i]->ld_lopc);
                printf("hipc %"  DW_PR_DUx " ",llbuf[i]->ld_hipc);
                printf("ld_section_offset %" DW_PR_DUx " ld_from_loclist %s ",llbuf[i]->ld_section_offset,llbuf[i]->ld_from_loclist?"debug_loc":"debug_info");
                printf("ld_cents %d ",llbuf[i]->ld_cents);
                for (j=0;j<llbuf[i]->ld_cents;j++)
                {
                    if (llbuf[i]->ld_s[j].lr_atom >= DW_OP_breg0 && llbuf[i]->ld_s[j].lr_atom <= DW_OP_breg31)
                        printf("location (a) %" DW_PR_DSd " ", (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
                    else
                        switch(llbuf[i]->ld_s[j].lr_atom)
                        {
                            case DW_OP_addr:
                                printf("location (b) 0x%" DW_PR_DUx " ", llbuf[i]->ld_s[j].lr_number);
                                break;
                            case DW_OP_consts: case DW_OP_const1s: case DW_OP_const2s: case DW_OP_const4s: case DW_OP_const8s:
                            case DW_OP_skip:
                            case DW_OP_bra:
                            case DW_OP_fbreg:
                                printf("location (c) %" DW_PR_DSd " ", (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
                                break;
                            case DW_OP_constu: case DW_OP_const1u: case DW_OP_const2u: case DW_OP_const4u: case DW_OP_const8u:
                            case DW_OP_pick:
                            case DW_OP_plus_uconst:
                            case DW_OP_regx:
                            case DW_OP_piece:
                            case DW_OP_deref_size:
                            case DW_OP_xderef_size:
                                printf("location (d) %" DW_PR_DUu " ", llbuf[i]->ld_s[j].lr_number);
                                break;
                            case DW_OP_bregx:
                                printf("location (e) reg%" DW_PR_DUu "(%" DW_PR_DSd ") ",llbuf[i]->ld_s[j].lr_number,llbuf[i]->ld_s[j].lr_number2);
                                break;
                            case DW_OP_GNU_uninit:
                            case DW_OP_GNU_encoded_addr:
                            case DW_OP_GNU_implicit_pointer:
                            case DW_OP_GNU_entry_value:
                                printf("DW_OP %d ",llbuf[i]->ld_s[j].lr_atom);
                            default:
                                break;
                        }
                    printf("\n");
                }
                dwarf_dealloc(dbg,llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
                dwarf_dealloc(dbg,llbuf[i], DW_DLA_LOCDESC);
            }
            dwarf_dealloc(dbg, llbuf, DW_DLA_LIST);
        }

        dwarf_dealloc(dbg,atlist[atcnt],DW_DLA_ATTR);
    }
    dwarf_dealloc(dbg,atlist,DW_DLA_LIST);

 done:
    printf("\n");
    return;

 panic:
    exit(1);
}

void get_die_and_siblings(Dwarf_Debug dbg, Dwarf_Die die)
{
    int status=0;
    Dwarf_Die child=0,sibling=0;
    TYPE_INFO *type_info=NULL;

    while (die)
    {
        Dwarf_Error error;
        int res;
        TRYCATCH(!(type_info=NEW(TYPE_INFO)),-1,panic,"allocating type_info");
        get_die_data(dbg,die,type_info);

        TRYCATCH((res=dwarf_child(die,&child,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_child");
        CATCH(res==DW_DLV_OK,0,get_die_and_siblings(dbg,child),"processing children if necessary");

        TRYCATCH((res=dwarf_siblingof(dbg,die,&sibling,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_siblingof");
        TRYCATCH(res==DW_DLV_NO_ENTRY,0,done,"checking for DW_DLV_NO_ENTRY"); /* Done at this level. */
        dwarf_dealloc(dbg,die,DW_DLA_DIE);
        die=sibling;
    }

 done:
    return;
 panic:
    exit(1);
}

void read_cu_list(Dwarf_Debug dbg,char *filename)
{
    int status=0;
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half version_stamp = 0;
    Dwarf_Unsigned abbrev_offset = 0;
    Dwarf_Half address_size = 0;
    Dwarf_Unsigned next_cu_header = 0;
    Dwarf_Error error;

    while (1)
    {
        Dwarf_Die cu_die = NULL;
        int res=dwarf_next_cu_header(dbg,&cu_header_length,&version_stamp,&abbrev_offset,&address_size,&next_cu_header,&error);
        TRYCATCH(res==DW_DLV_NO_ENTRY,0,done,"checking DW_DLV_NO_ENTRY");
        TRYCATCH(res==DW_DLV_ERROR,-1,panic,"checking dwarf_next_cu_header");
        TRYCATCH((res=dwarf_siblingof(dbg,NULL,&cu_die,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_siblingof on CU die");
        TRYCATCH(res==DW_DLV_NO_ENTRY,-1,panic,"checking for DW_DLV_NO_ENTRY in dwarf_siblingof on CU die");
        get_die_and_siblings(dbg,cu_die);
    }

 done:
    return;
 panic:
    exit(1);
}


int dwarf2edict(char *filename)
{
    int status=0;
    int import_fd = -1;
    Dwarf_Debug dbg = NULL;
    Dwarf_Error error;

    STRY((import_fd=open(filename,O_RDONLY))<0,"opening dward2edict input file %s",filename);

    TRYCATCH((dwarf_init(import_fd,DW_DLC_READ,NULL,NULL,&dbg,&error) != DW_DLV_OK),-1,close_file,"initializing dwarf reader");
    read_cu_list(dbg,filename);
    TRYCATCH((dwarf_finish(dbg,&error) != DW_DLV_OK),-1,close_file,"finalizing dwarf reader");

    close_file:
    close(import_fd);
    done:
    return status;
}

