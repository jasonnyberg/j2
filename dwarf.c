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
#include "libdwarf/dwarf.h"
#include "libdwarf/libdwarf.h"
#include "util.h"

#define END "\n"
#define SPC " "

void get_die_and_siblings(FILE *ofile,Dwarf_Debug dbg, Dwarf_Die in_die,int level);

#define DIE(cond) TRYCATCH(cond!=DW_DLV_OK,-1,panic,END)
#define SKIP(cond,followup) if (cond==DW_DLV_OK) followup

typedef void (*attrib_handler)(FILE *ofile,Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr);


void dump_attrib(FILE *ofile,Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr)
{
    int status=0;
    Dwarf_Error error=0;

    Dwarf_Signed vint;
    Dwarf_Unsigned vuint;
    Dwarf_Half vshort;
    const char *vcstr;
    char *vstr;
    Dwarf_Off voffset;
    Dwarf_Addr vaddr;
    Dwarf_Bool vbool;
    Dwarf_Block *vblock;

    DIE(dwarf_whatattr(*attr,&vshort,&error));
    switch (vshort) {
        case DW_AT_data_member_location:
        case DW_AT_const_value:
            break;
        default:
            //break;
            return;
    }

    /*
    fprintf(ofile,"[" END);
    DIE(dwarf_whatattr(*attr,&vshort,&error));
    DIE(dwarf_get_AT_name(vshort,&vcstr));
    fprintf(ofile,"[%d]@attr attr<[%s]@name" SPC,vshort,vcstr);

    DIE(dwarf_whatform(*attr,&vshort,&error));
    DIE(dwarf_get_FORM_name(vshort,&vcstr));
    fprintf(ofile,"[%d]@form [%s]@form.name" SPC,vshort,vcstr);

    DIE(dwarf_whatform_direct(*attr,&vshort,&error));
    DIE(dwarf_get_FORM_name(vshort,&vcstr));
    fprintf(ofile,"[%d]@form_direct [%s]@form_direct.name" SPC,vshort,vcstr);

    //SKIP(dwarf_formref(*attr,&voffset,&error),fprintf(ofile,"[%d]@formref" SPC,(int) voffset));
    SKIP(dwarf_global_formref(*attr,&voffset,&error),fprintf(ofile,"[%d]@global_formref" SPC,(int) voffset));
    SKIP(dwarf_formaddr(*attr,&vaddr,&error),fprintf(ofile,"[0x%x]@formaddr" SPC,(int) vaddr));
    SKIP(dwarf_formflag(*attr,&vbool,&error),fprintf(ofile,"[%d]@formflag" SPC,vbool));
    SKIP(dwarf_formudata(*attr,&vuint,&error),fprintf(ofile,"[0x%x]@udata" SPC,(unsigned) vuint));
    //SKIP(dwarf_formblock(*attr,&vblock,&error),fprintf(ofile,"[%d]@block" SPC,vblock));
    SKIP(dwarf_formstring(*attr,&vstr,&error),fprintf(ofile,"[%s]@string" SPC,vstr));
    fprintf(ofile,END "]@full_data" END);
    */

    fprintf(ofile,"[");
    //SKIP(dwarf_formref(*attr,&voffset,&error),fprintf(ofile,"[0x%" DW_PR_DSx "]@formref" SPC,voffset));
    //SKIP(dwarf_global_formref(*attr,&voffset,&error),fprintf(ofile,"[0x%" DW_PR_DSx "]@global_formref" SPC,voffset));
    SKIP(dwarf_formaddr(*attr,&vaddr,&error),fprintf(ofile,"0x%" DW_PR_DUx SPC,vaddr));
    SKIP(dwarf_formflag(*attr,&vbool,&error),fprintf(ofile,"%" DW_PR_DSd SPC,vbool));
    SKIP(dwarf_formudata(*attr,&vuint,&error),fprintf(ofile,"0x%" DW_PR_DUx SPC,vuint));
    //SKIP(dwarf_formblock(*attr,&vblock,&error),fprintf(ofile,"0x%" DW_PR_DUx SPC,vblock->bl_len));
    SKIP(dwarf_formstring(*attr,&vstr,&error),fprintf(ofile,"%s" SPC,vstr));
    fprintf(ofile,"]");

    DIE(dwarf_get_AT_name(vshort,&vcstr));
    fprintf(ofile,"@%s" END,vcstr+6); // skip DW_AT_

    Dwarf_Locdesc **llbuf;
    if (dwarf_loclist_n(*attr,&llbuf,&vint,&error) == DW_DLV_OK)
    {
        int i,j;
        for (i=0;i<vint;i++)
        {
            fprintf(ofile,"[%d]@loclist loclist<",i);

            fprintf(ofile,"[%" DW_PR_DUx "]@lowpc" SPC,llbuf[i]->ld_lopc);
            fprintf(ofile,"[%" DW_PR_DUx "]@hipc" SPC,llbuf[i]->ld_hipc);
            fprintf(ofile,"[%" DW_PR_DUx "]@section_offset.%s" SPC,llbuf[i]->ld_section_offset,llbuf[i]->ld_from_loclist?"debug_loc":"debug_info");
            fprintf(ofile,"[%d]@ld_cents" SPC,llbuf[i]->ld_cents);
            for (j=0;j<llbuf[i]->ld_cents;j++)
            {
                if (llbuf[i]->ld_s[j].lr_atom >= DW_OP_breg0 && llbuf[i]->ld_s[j].lr_atom <= DW_OP_breg31)
                    fprintf(ofile,"[%" DW_PR_DSd "]@-location" SPC, (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
                else
                    switch(llbuf[i]->ld_s[j].lr_atom)
                    {
                        case DW_OP_addr:
                            fprintf(ofile,"[0x%" DW_PR_DUx "]@-location" SPC, llbuf[i]->ld_s[j].lr_number);
                            break;
                        case DW_OP_const1s:
                        case DW_OP_const2s:
                        case DW_OP_const4s:
                        case DW_OP_const8s:
                        case DW_OP_consts:
                        case DW_OP_skip:
                        case DW_OP_bra:
                        case DW_OP_fbreg:
                            fprintf(ofile,"[%" DW_PR_DSd "]@-location" SPC, (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
                            break;
                        case DW_OP_const1u:
                        case DW_OP_const2u:
                        case DW_OP_const4u:
                        case DW_OP_const8u:
                        case DW_OP_constu:
                        case DW_OP_pick:
                        case DW_OP_plus_uconst:
                        case DW_OP_regx:
                        case DW_OP_piece:
                        case DW_OP_deref_size:
                        case DW_OP_xderef_size:
                            fprintf(ofile,"[%" DW_PR_DUu "]@-location" SPC, llbuf[i]->ld_s[j].lr_number);
                            break;
                        case DW_OP_bregx:
                            fprintf(ofile,"[reg%" DW_PR_DUu "(%" DW_PR_DSd ")]@-location" SPC,
                                   llbuf[i]->ld_s[j].lr_number,
                                   llbuf[i]->ld_s[j].lr_number2);
                            break;
                        default:
                            break;
                    }
            }

            fprintf(ofile,">/" END); // loclist

            dwarf_dealloc(dbg,llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
            dwarf_dealloc(dbg,llbuf[i], DW_DLA_LOCDESC);
        }
        dwarf_dealloc(dbg, llbuf, DW_DLA_LIST);
    }

    return;

 panic:
    exit(1);
}


void dump_attrib_base(FILE *ofile,Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr)
{
    int status=0;
    Dwarf_Error error=0;
    Dwarf_Half vshort;
    Dwarf_Off die_offset;
    Dwarf_Off voffset;

    dwarf_dieoffset(die,&die_offset,&error);
    DIE(dwarf_whatattr(*attr,&vshort,&error));
    if (vshort==DW_AT_type)
        SKIP(dwarf_global_formref(*attr,&voffset,&error),
             fprintf(ofile,"reflection.module<[id[0x%" DW_PR_DUx "].die@id[0x%" DW_PR_DUx "].die.base]@finalize>/" SPC,(int) voffset,(int) die_offset));
    return;

 panic:
    exit(1);
}

void traverse_attribs(FILE *ofile,Dwarf_Debug dbg,Dwarf_Die die,attrib_handler handler)
{
    Dwarf_Signed atcnt=0;
    Dwarf_Attribute *atlist=NULL;
    Dwarf_Error error=0;
    int errv=dwarf_attrlist(die,&atlist,&atcnt,&error);

    if (errv == DW_DLV_OK)
    {
        int i;
        for (i=0;i<atcnt;i++)
        {
            handler(ofile,dbg,die,&atlist[i]);
            dwarf_dealloc(dbg,atlist[i],DW_DLA_ATTR);
        }
        dwarf_dealloc(dbg,atlist,DW_DLA_LIST);
    }
}

void print_die_data(FILE *ofile,Dwarf_Debug dbg,Dwarf_Die die,int level)
{
    int status=0;
    char *diename = NULL,*name=NULL;
    Dwarf_Error error = 0;
    Dwarf_Half tag = 0;
    const char *tagname = 0;
    Dwarf_Die child = NULL;
    Dwarf_Off die_offset=0;
    Dwarf_Unsigned vuint;
    Dwarf_Addr vaddr;

    int res;

    static char *indent="                                                                                        ";

    TRYCATCH((res=dwarf_diename(die,&diename,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_diename , level %d" END,level);
    name=diename?diename:"__anon__";
    TRYCATCH(dwarf_tag(die,&tag,&error) != DW_DLV_OK,-1,panic,"checking dwarf_tag , level %d" END,level);
    TRYCATCH(dwarf_get_TAG_name(tag,&tagname) != DW_DLV_OK,-1,panic,"checking dwarf_get_TAG_name , level %d" END,level);
    TRYCATCH(dwarf_dieoffset(die,&die_offset,&error) !=  DW_DLV_OK,-1,panic,"checking dwarf_dieoffset, level %d" END,level);

    fprintf(ofile,END);
    fstrnprint(ofile,indent,level*2);
    fprintf(ofile,"[%s]@-%2$s -%2$s<[%2$s]@tag" SPC,name,tagname+7);

    dwarf_dealloc(dbg,diename,DW_DLA_STRING);
    SKIP(dwarf_lowpc(die,&vaddr,&error),fprintf(ofile,"[0x%" DW_PR_DUx "]@lowpc" SPC,vaddr));
    SKIP(dwarf_highpc(die,&vaddr,&error),fprintf(ofile,"[0x%" DW_PR_DUx "]@highpc" SPC,vaddr));
    SKIP(dwarf_bytesize(die,&vuint,&error),fprintf(ofile,"[%" DW_PR_DUu "]@bytesize" SPC,vuint));
    SKIP(dwarf_bitsize(die,&vuint,&error),fprintf(ofile,"[%" DW_PR_DUu "]@bitsize" SPC,vuint));
    SKIP(dwarf_bitoffset(die,&vuint,&error),fprintf(ofile,"[%" DW_PR_DUu "]@bitoffset" SPC,vuint));
    SKIP(dwarf_srclang(die,&vuint,&error),fprintf(ofile,"[%" DW_PR_DUu "]@srclang" SPC,vuint));
    SKIP(dwarf_arrayorder(die,&vuint,&error),fprintf(ofile,"[%" DW_PR_DUu "]@arrayorder" SPC,vuint));

    traverse_attribs(ofile,dbg,die,dump_attrib);

    switch(tag)
    {
        case DW_TAG_compile_unit:
        case DW_TAG_subprogram:
        case DW_TAG_formal_parameter:
        case DW_TAG_enumeration_type:
        case DW_TAG_enumerator:
        case DW_TAG_variable:
        case DW_TAG_structure_type:
        case DW_TAG_base_type:
        case DW_TAG_member:
        case DW_TAG_typedef:
            break;
    }


    TRYCATCH((res=dwarf_child(die,&child,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_child , level %d" END,level);
    TRYCATCH(res!=DW_DLV_OK,0,done,"checking if die finished" END);

    get_die_and_siblings(ofile,dbg,child,level+1);

    done:
    fprintf(ofile,END);
    fstrnprint(ofile,indent,level*2);
    traverse_attribs(ofile,dbg,die,dump_attrib_base);
    fprintf(ofile,"> reflection.module<@id[0x%" DW_PR_DUx "].die>/ /" END,(int) die_offset); // scope element left on anon stack upon closure!
    return;

 panic:
    exit(1);
}

void get_die_and_siblings(FILE *ofile,Dwarf_Debug dbg, Dwarf_Die in_die,int level)
{
    int status=0;
    Dwarf_Die cur_die=in_die,sib_die=0;

    while (cur_die)
    {
        Dwarf_Error error;
        int res;
        print_die_data(ofile,dbg,cur_die,level);
        TRYCATCH((res=dwarf_siblingof(dbg,cur_die,&sib_die,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_siblingof , level %d" END,level);
        TRYCATCH(res==DW_DLV_NO_ENTRY,0,done,"checking for DW_DLV_NO_ENTRY"); /* Done at this level. */
        if(cur_die!=in_die)
            dwarf_dealloc(dbg,cur_die,DW_DLA_DIE);
        cur_die=sib_die;
    }

 done:
    return;
 panic:
    exit(1);
}

void read_cu_list(FILE *ofile,Dwarf_Debug dbg,char *module)
{
    int status=0;
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half version_stamp = 0;
    Dwarf_Unsigned abbrev_offset = 0;
    Dwarf_Half address_size = 0;
    Dwarf_Unsigned next_cu_header = 0;
    Dwarf_Error error;
    int cu_number = 0;

    fprintf(ofile,"[%s]@reflection.module reflection.module<" END,module);

    while (1)
    {
        Dwarf_Die no_die = NULL;
        Dwarf_Die cu_die = NULL;
        int res=dwarf_next_cu_header(dbg,&cu_header_length,&version_stamp,&abbrev_offset,&address_size,&next_cu_header,&error);

        TRYCATCH(res==DW_DLV_NO_ENTRY,0,done,"checking DW_DLV_NO_ENTRY");
        TRYCATCH(res==DW_DLV_ERROR,-1,panic,"checking dwarf_next_cu_header" END);
        TRYCATCH((res=dwarf_siblingof(dbg,no_die,&cu_die,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_siblingof on CU die" END);
        TRYCATCH(res==DW_DLV_NO_ENTRY,-1,panic,"checking for DW_DLV_NO_ENTRY in dwarf_siblingof on CU die" END);

        get_die_and_siblings(ofile,dbg,cu_die,0);
        dwarf_dealloc(dbg,cu_die,DW_DLA_DIE);
        cu_number++;
    }

    done:
    fprintf(ofile,END END); // instantiate dependencies
    fprintf(ofile,"[!]!/finalize [/]!/reflection.module.id" END); // instantiate dependencies
    fprintf(ofile,">/" END END); // instantiate dependencies
    return;
 panic:
    exit(1);
}


int dwarf2edict(char *import,char *export)
{
    int status=0;
    Dwarf_Debug dbg = NULL;
    int import_fd = -1;
    Dwarf_Error error;
    FILE *export_file=NULL;

    STRY((import_fd=open(import,O_RDONLY))<0,"opening dward2edict input file %s",import);
    TRYCATCH(!(export_file=fopen(export,"w")),status,close_import,"opening dwarf2edict output file %s",export);

    TRYCATCH((dwarf_init(import_fd,DW_DLC_READ,NULL,NULL,&dbg,&error) != DW_DLV_OK),-1,close_export,"initializing dwarf reader");
    read_cu_list(export_file,dbg,import);
    TRYCATCH((dwarf_finish(dbg,&error) != DW_DLV_OK),-1,close_export,"finalizing dwarf reader");

    close_export:
    fclose(export_file);
    close_import:
    close(import_fd);
    done:
    return status;
}

