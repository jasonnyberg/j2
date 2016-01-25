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

void get_die_and_siblings(Dwarf_Debug dbg, Dwarf_Die in_die,int level);

#define DIE(cond) TRY(cond!=DW_DLV_OK,-1,panic,END)
#define SKIP(cond,followup) if (cond==DW_DLV_OK) followup

typedef void (*attrib_handler)(Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr);

void dump_attrib(Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr)
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
    //Dwarf_Block *vblock;

    printf("[]@attribs attribs<" END);

    DIE(dwarf_whatattr(*attr,&vshort,&error));
    DIE(dwarf_get_AT_name(vshort,&vcstr));
    printf("[%d]@attr [%s]@attr_name" END,vshort,vcstr);

    DIE(dwarf_whatform(*attr,&vshort,&error));
    DIE(dwarf_get_FORM_name(vshort,&vcstr));
    printf("[%d]@form [%s]@form_name" END,vshort,vcstr);

    DIE(dwarf_whatform_direct(*attr,&vshort,&error));
    DIE(dwarf_get_FORM_name(vshort,&vcstr));
    printf("[%d]@form_direct [%s]@form_direct_name" END,vshort,vcstr);

    //SKIP(dwarf_formref(*attr,&voffset,&error),printf("[%d]@formref" END,(int) voffset));
    SKIP(dwarf_global_formref(*attr,&voffset,&error),printf("[%d]@global_formref" END,(int) voffset));
    SKIP(dwarf_formaddr(*attr,&vaddr,&error),printf("[%d]@formaddr" END,(int) vaddr));
    SKIP(dwarf_formflag(*attr,&vbool,&error),printf("[%d]@formflag" END,vbool));
    SKIP(dwarf_formudata(*attr,&vuint,&error),printf("[0x%x]@udata" END,(unsigned) vuint));
    //SKIP(dwarf_formblock(*attr,&vblock,&error),printf("[%d]@block" END,vblock));
    SKIP(dwarf_formstring(*attr,&vstr,&error),printf("[%s]@string" END,vstr));

    Dwarf_Locdesc **llbuf;
    if (dwarf_loclist_n(*attr,&llbuf,&vint,&error) == DW_DLV_OK)
    {
        int i,j;
        for (i=0;i<vint;i++)
        {
            printf("[%d]@loclist loclist<" END,i);

            printf("[%" DW_PR_DUx "]@lowpc" END,llbuf[i]->ld_lopc);
            printf("[%" DW_PR_DUx "]@hipc" END,llbuf[i]->ld_hipc);
            printf("[%" DW_PR_DUx "]@section_offset.%s" END,
                   llbuf[i]->ld_section_offset,
                   llbuf[i]->ld_from_loclist?"debug_loc":"debug_info");
            printf("[%d]@ld_cents" END,llbuf[i]->ld_cents);
            for (j=0;j<llbuf[i]->ld_cents;j++)
            {
                if (llbuf[i]->ld_s[j].lr_atom >= DW_OP_breg0 && llbuf[i]->ld_s[j].lr_atom <= DW_OP_breg31)
                    printf("[%" DW_PR_DSd "]@-location" END, (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
                else
                    switch(llbuf[i]->ld_s[j].lr_atom)
                    {
                        case DW_OP_addr:
                            printf("[0x%" DW_PR_DUx "]@-location" END, llbuf[i]->ld_s[j].lr_number);
                            break;
                        case DW_OP_const1s:
                        case DW_OP_const2s:
                        case DW_OP_const4s:
                        case DW_OP_const8s:
                        case DW_OP_consts:
                        case DW_OP_skip:
                        case DW_OP_bra:
                        case DW_OP_fbreg:
                            printf("%" DW_PR_DSd "]@-location" END, (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
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
                            printf("%" DW_PR_DUu "]@-location" END, llbuf[i]->ld_s[j].lr_number);
                            break;
                        case DW_OP_bregx:
                            printf("[reg%" DW_PR_DUu "(%" DW_PR_DSd ")]@-location" END,
                                   llbuf[i]->ld_s[j].lr_number,
                                   llbuf[i]->ld_s[j].lr_number2);
                            break;
                        default:
                            break;
                    }
                printf(">" END);
            }

            dwarf_dealloc(dbg,llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
            dwarf_dealloc(dbg,llbuf[i], DW_DLA_LOCDESC);
        }
        dwarf_dealloc(dbg, llbuf, DW_DLA_LIST);
    }

    printf(">" END);
    printf("[----------------------------------------------------------]" END);
    return;

 panic:
    exit(1);
}

void dump_attrib_location(Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr)
{
    int status=0;
    Dwarf_Error error=0;

    Dwarf_Signed vint;
    Dwarf_Half vshort;

    DIE(dwarf_whatattr(*attr,&vshort,&error));
    if (vshort==DW_AT_location)
    {
        Dwarf_Locdesc **llbuf;
        if (dwarf_loclist_n(*attr,&llbuf,&vint,&error) == DW_DLV_OK)
        {
            int i,j;
            for (i=0;i<vint;i++)
            {
                for (j=0;j<llbuf[i]->ld_cents;j++)
                {
                    if (llbuf[i]->ld_s[j].lr_atom >= DW_OP_breg0 && llbuf[i]->ld_s[j].lr_atom <= DW_OP_breg31)
                        printf("[%" DW_PR_DSd "]@-location" END, (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
                    else
                        switch(llbuf[i]->ld_s[j].lr_atom)
                        {
                            case DW_OP_addr:
                                printf("[0x%" DW_PR_DUx "]@-location" END, llbuf[i]->ld_s[j].lr_number);
                                break;
                            case DW_OP_const1s:
                            case DW_OP_const2s:
                            case DW_OP_const4s:
                            case DW_OP_const8s:
                            case DW_OP_consts:
                            case DW_OP_skip:
                            case DW_OP_bra:
                            case DW_OP_fbreg:
                                printf("[%" DW_PR_DSd "]@-location" END, (Dwarf_Signed) llbuf[i]->ld_s[j].lr_number);
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
                                    printf("[%" DW_PR_DUu "]@-location" END, llbuf[i]->ld_s[j].lr_number);
                                break;
                            case DW_OP_bregx:
                                printf("[bregx_%" DW_PR_DUu "([%" DW_PR_DSd "])]@-location" END,
                                       llbuf[i]->ld_s[j].lr_number,
                                       llbuf[i]->ld_s[j].lr_number2);
                                break;
                            default:
                                break;
                        }
                    printf(END);
                }

                dwarf_dealloc(dbg,llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
                dwarf_dealloc(dbg,llbuf[i], DW_DLA_LOCDESC);
            }
            dwarf_dealloc(dbg, llbuf, DW_DLA_LIST);
        }
    }

    return;

 panic:
    exit(1);
}

void dump_attrib_base(Dwarf_Debug dbg,Dwarf_Die die,Dwarf_Attribute *attr)
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
             printf("[die_offsets.%d@die_offsets.%d.base]@module.deps" END,
                    (int) voffset,(int) die_offset));
    return;

 panic:
    exit(1);
}

void traverse_attribs(Dwarf_Debug dbg,Dwarf_Die die,attrib_handler handler)
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
            handler(dbg,die,&atlist[i]);
            dwarf_dealloc(dbg,atlist[i],DW_DLA_ATTR);
        }
        dwarf_dealloc(dbg,atlist,DW_DLA_LIST);
    }
}

void print_die_data(Dwarf_Debug dbg,Dwarf_Die die,int level)
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

    TRY((res=dwarf_diename(die,&diename,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_diename , level %d" END,level);
    name=diename?diename:"anonymous";
    TRY(dwarf_tag(die,&tag,&error) != DW_DLV_OK,-1,panic,"checking dwarf_tag , level %d" END,level);
    TRY(dwarf_get_TAG_name(tag,&tagname) != DW_DLV_OK,-1,panic,"checking dwarf_get_TAG_name , level %d" END,level);
    TRY(dwarf_dieoffset(die,&die_offset,&error) !=  DW_DLV_OK,-1,panic,"checking dwarf_dieoffset, level %d" END,level);

    printf("[%s]@children" END END,name);
    printf("reflection.module@module" END);
    printf("children@module.die_offsets.%d" END,(int) die_offset);
    printf("module.tags+%s@children.tag" END,tagname+7); // skip "DW_TAG_"
    //printf("children@module.tags=%s.%s" END,tagname+7,name); // skip "DW_TAG_"
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

    traverse_attribs(dbg,die,dump_attrib_base);

    printf("/module" END);
    printf("children<" END);

    SKIP(dwarf_lowpc(die,&vaddr,&error),printf("[%d]@lowpc" END,(int) vaddr));
    SKIP(dwarf_highpc(die,&vaddr,&error),printf("[%d]@highpc" END,(int) vaddr));
    SKIP(dwarf_bytesize(die,&vuint,&error),printf("[%d]@bytesize" END,(int) vuint));
    SKIP(dwarf_bitsize(die,&vuint,&error),printf("[%d]@bitsize" END,(int) vuint));
    SKIP(dwarf_bitoffset(die,&vuint,&error),printf("[%d]@bitoffset" END,(int) vuint));
    SKIP(dwarf_srclang(die,&vuint,&error),printf("[%d]@srclang" END,(int) vuint));
    SKIP(dwarf_arrayorder(die,&vuint,&error),printf("[%d]@arrayorder" END,(int) vuint));

    //traverse_attribs(dbg,die,dump_attrib);
    traverse_attribs(dbg,die,dump_attrib_location);
    dwarf_dealloc(dbg,diename,DW_DLA_STRING);

    TRY((res=dwarf_child(die,&child,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_child , level %d" END,level);
    TRY(res!=DW_DLV_OK,0,done,"checking if die finished" END);

    get_die_and_siblings(dbg,child,level+1);

 done:
    printf(">" END);
    return;

 panic:
    exit(1);
}

void get_die_and_siblings(Dwarf_Debug dbg, Dwarf_Die in_die,int level)
{
    int status=0;
    Dwarf_Die cur_die,sib_die;

    for(cur_die=in_die,sib_die=0;cur_die;cur_die=sib_die)
    {
        Dwarf_Error error;
        int res;
        print_die_data(dbg,cur_die,level);
        TRY((res=dwarf_siblingof(dbg,cur_die,&sib_die,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_siblingof , level %d" END,level);
        TRY(res==DW_DLV_NO_ENTRY,0,done,"checking for DW_DLV_NO_ENTRY"); /* Done at this level. */
        if(cur_die!=in_die)
            dwarf_dealloc(dbg,cur_die,DW_DLA_DIE);
        cur_die=sib_die;
    }

 done:
    return;
 panic:
    exit(1);
}

void read_cu_list(Dwarf_Debug dbg,char *module)
{
    int status=0;
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half version_stamp = 0;
    Dwarf_Unsigned abbrev_offset = 0;
    Dwarf_Half address_size = 0;
    Dwarf_Unsigned next_cu_header = 0;
    Dwarf_Error error;
    int cu_number = 0;

    printf("[%s]@reflection.module reflection.module<" END,module); // enter "reflection.module" namespace

    while (1)
    {
        Dwarf_Die no_die = NULL;
        Dwarf_Die cu_die = NULL;
        int res=dwarf_next_cu_header(dbg,&cu_header_length,&version_stamp,&abbrev_offset,&address_size,&next_cu_header,&error);

        TRY(res==DW_DLV_NO_ENTRY,0,done,"checking DW_DLV_NO_ENTRY");
        TRY(res==DW_DLV_ERROR,-1,panic,"checking dwarf_next_cu_header" END);
        TRY((res=dwarf_siblingof(dbg,no_die,&cu_die,&error))==DW_DLV_ERROR,-1,panic,"checking dwarf_siblingof on CU die" END);
        TRY(res==DW_DLV_NO_ENTRY,-1,panic,"checking for DW_DLV_NO_ENTRY in dwarf_siblingof on CU die" END);

        get_die_and_siblings(dbg,cu_die,0);
        dwarf_dealloc(dbg,cu_die,DW_DLA_DIE);
        cu_number++;
    }

    printf(END "[!]!deps [/]!die_offsets>" END END); // instantiate dependencies, remove die_offsets, end "reflection.module" namespace

 done:
    return;
 panic:
    exit(1);
}


int main(int argc, char **argv)
{
    Dwarf_Debug dbg = NULL;
    int fd = -1;
    char *filepath = "<stdin>";
    Dwarf_Error error;

    if(argc < 2) fd = 0; /* stdin */
    else
    {
        filepath = argv[1];
        fd = open(filepath,O_RDONLY);
    }

    if(fd < 0) printf("Failure attempting to open %s" END,filepath);

    if(dwarf_init(fd,DW_DLC_READ,NULL,NULL,&dbg,&error) != DW_DLV_OK)
        printf("Giving up, cannot do DWARF processing" END), exit(1);

    read_cu_list(dbg,filepath);

    if(dwarf_finish(dbg,&error) != DW_DLV_OK)
        printf("dwarf_finish failed!" END);

    close(fd);
    return 0;
}

