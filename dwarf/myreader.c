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
/* simplereader.c
   This is an example of code reading dwarf .debug_info.
   It is kept as simple as possible to expose essential features.
   It does not do all possible error reporting or error handling.

   To use, try
       make
       ./simplereader simplereader
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

void get_die_and_siblings(Dwarf_Debug dbg, Dwarf_Die in_die,int level);

#define DIE(cond) TRY(cond!=DW_DLV_OK,-1,panic,"\n")
#define SKIP(cond,followup) if (cond==DW_DLV_OK) followup

void dump_attrib(Dwarf_Debug dbg,Dwarf_Attribute *attr)
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
    
    DIE(dwarf_whatattr(*attr,&vshort,&error));
    DIE(dwarf_get_AT_name(vshort,&vcstr));
    printf("[%d]@attr [%s]@attr_name\n",vshort,vcstr);
    
    DIE(dwarf_whatform(*attr,&vshort,&error));
    DIE(dwarf_get_FORM_name(vshort,&vcstr));
    printf("[%d]@form [%s]@form_name\n",vshort,vcstr);
    
    DIE(dwarf_whatform_direct(*attr,&vshort,&error));
    DIE(dwarf_get_FORM_name(vshort,&vcstr));
    printf("[%d]@form_direct [%s]@form_direct_name\n",vshort,vcstr);

    SKIP(dwarf_formref(*attr,&voffset,&error),printf("[%d]@formref\n",(int) voffset));
    SKIP(dwarf_global_formref(*attr,&voffset,&error),printf("[%d]@global_formref\n",(int) voffset));
    SKIP(dwarf_formaddr(*attr,&vaddr,&error),printf("[%d]@formaddr\n",(int) vaddr));
    SKIP(dwarf_formflag(*attr,&vbool,&error),printf("[%d]@formflag\n",vbool));
    SKIP(dwarf_formudata(*attr,&vuint,&error),printf("[%d]@udata\n",(int) vuint));
    SKIP(dwarf_formsdata(*attr,&vint,&error),printf("[%d]@udata\n",(int) vint));
    //SKIP(dwarf_formblock(*attr,&vblock,&error),printf("[%d]@block\n",vblock));
    SKIP(dwarf_formstring(*attr,&vstr,&error),printf("[%s]@string\n",vstr));

    Dwarf_Locdesc **llbuf;
    if (dwarf_loclist_n(*attr,&llbuf,&vint,&error) == DW_DLV_OK)
    {
        int i;
        for (i=0;i<vint;i++)
        {
            printf("[unimplemented]@loclist.%d\n",i);
            dwarf_dealloc(dbg,llbuf[i]->ld_s, DW_DLA_LOC_BLOCK);
            dwarf_dealloc(dbg,llbuf[i], DW_DLA_LOCDESC);
        }
        dwarf_dealloc(dbg, llbuf, DW_DLA_LIST);
    }
    
    return;

 panic:
    exit(1);
}

void traverse_attribs(Dwarf_Debug dbg,Dwarf_Die die)
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
            printf("[%d]@attribs attribs<\n",i);
            dump_attrib(dbg,&atlist[i]);
            printf(">\n");
            
            dwarf_dealloc(dbg,atlist[i],DW_DLA_ATTR);
        }
        dwarf_dealloc(dbg,atlist,DW_DLA_LIST);
    }
}        

void print_die_data(Dwarf_Debug dbg,Dwarf_Die die,int level)
{
    int status=0;
    char *name = 0;
    Dwarf_Error error = 0;
    Dwarf_Half tag = 0;
    const char *tagname = 0;
    Dwarf_Die child = NULL;
    Dwarf_Off die_offset=0;
    Dwarf_Unsigned vuint;
    Dwarf_Addr vaddr;

    int res;

    TRY((res=dwarf_diename(die,&name,&error))==DW_DLV_ERROR,-1,panic,"Error in dwarf_diename , level %d\n",level);
    TRY(res==DW_DLV_NO_ENTRY,0,done,"DW_DLV_NO_ENTRY");
    
    TRY(dwarf_tag(die,&tag,&error) != DW_DLV_OK,-1,panic,"Error in dwarf_tag , level %d\n",level);
    TRY(dwarf_get_TAG_name(tag,&tagname) != DW_DLV_OK,-1,panic,"Error in dwarf_get_TAG_name , level %d\n",level);
    TRY(dwarf_dieoffset(die,&die_offset,&error) !=  DW_DLV_OK,-1,panic,"Error in dwarf_dieoffset, level %d\n",level);
    
    printf("types@types [%s]@types.%d /types\n",name,(int) die_offset);
    printf("tags@tags tags+%2$s@types.%1$d.tagname types.%1$d@tagname.%2$s /tags\n",(int) die_offset,tagname);
    //printf("%d<[%s]@type\n",(int) die_offset,tagname);
    printf("types.%d<\n",(int) die_offset);
    
    SKIP(dwarf_lowpc(die,&vaddr,&error),printf("[%d]@lowpc\n",(int) vaddr));
    SKIP(dwarf_highpc(die,&vaddr,&error),printf("[%d]@highpc\n",(int) vaddr));
    SKIP(dwarf_bytesize(die,&vuint,&error),printf("[%d]@bytesize\n",(int) vuint));
    SKIP(dwarf_bitsize(die,&vuint,&error),printf("[%d]@bitsize\n",(int) vuint));
    SKIP(dwarf_bitoffset(die,&vuint,&error),printf("[%d]@bitoffset\n",(int) vuint));
    SKIP(dwarf_srclang(die,&vuint,&error),printf("[%d]@srclang\n",(int) vuint));
    SKIP(dwarf_arrayorder(die,&vuint,&error),printf("[%d]@arrayorder\n",(int) vuint));
    
    // traverse_attribs(dbg,die);
   
    dwarf_dealloc(dbg,name,DW_DLA_STRING);
    
    TRY((res=dwarf_child(die,&child,&error))==DW_DLV_ERROR,-1,panic,"Error in dwarf_child , level %d\n",level);
    TRY(res!=DW_DLV_OK,-1,done_die,"");
    
    printf("[]@children children<\n");
    get_die_and_siblings(dbg,child,level+1);
    printf(">\n");

 done_die:
    printf(">\n");

 done:
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
        TRY((res=dwarf_siblingof(dbg,cur_die,&sib_die,&error))==DW_DLV_ERROR,-1,panic,"Error in dwarf_siblingof , level %d\n",level);
        TRY(res==DW_DLV_NO_ENTRY,0,done,"DW_DLV_NO_ENTRY"); /* Done at this level. */
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

    while (1)
    {
        Dwarf_Die no_die = NULL;
        Dwarf_Die cu_die = NULL;
        int res=dwarf_next_cu_header(dbg,&cu_header_length,&version_stamp,&abbrev_offset,&address_size,&next_cu_header,&error);

        TRY(res==DW_DLV_NO_ENTRY,0,done,"DW_DLV_NO_ENTRY");
        TRY(res==DW_DLV_ERROR,-1,panic,"Error in dwarf_next_cu_header\n");
        TRY((res=dwarf_siblingof(dbg,no_die,&cu_die,&error))==DW_DLV_ERROR,-1,panic,"Error in dwarf_siblingof on CU die\n");
        TRY(res==DW_DLV_NO_ENTRY,-1,panic,"no entry! in dwarf_siblingof on CU die\n");
    
        printf("[%s]@reflection.module reflection.module<\n",module);
        printf("[]@types\n");
        printf("[]@tags\n");
        get_die_and_siblings(dbg,cu_die,0);
        printf("> [module]/\n");
        dwarf_dealloc(dbg,cu_die,DW_DLA_DIE);
        cu_number++;
    }

 done:
    return;
 panic:
    exit(1);
}


int main(int argc, char **argv)
{
    Dwarf_Debug dbg = NULL;
    int fd = -1;
    const char *filepath = "<stdin>";
    Dwarf_Error error;

    if(argc < 2) fd = 0; /* stdin */
    else
    {
        filepath = argv[1];
        fd = open(filepath,O_RDONLY);
    }
    
    if(fd < 0) printf("Failure attempting to open %s\n",filepath);
    
    if(dwarf_init(fd,DW_DLC_READ,NULL,NULL,&dbg,&error) != DW_DLV_OK)
        printf("Giving up, cannot do DWARF processing\n"), exit(1);
    
    read_cu_list(dbg,"main");
    
    if(dwarf_finish(dbg,&error) != DW_DLV_OK)
        printf("dwarf_finish failed!\n");
    
    close(fd);
    return 0;
}

