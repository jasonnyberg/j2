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

//#define _GNU_SOURCE
#define _C99
#include <libelf.h>
#include <elfutils/libdwelf.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h> // dlopen/dlsym/dlclose

#include "util.h"
#include "vm.h"
#include "extensions.h"

extern int square(int a) { return a*a; }
extern int minus(int a,int b) { return a-b; }
extern int string(char *s) { fprintf(OUTFILE,"%s\n",s); }
extern LTV *ltv_coersion_test(LTV *ltv) { print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0); return ltv; } // no change to stack means success

extern FILE *get_stdin() { return stdin; }

extern FILE *get_stdout() { return stdout; }
extern FILE *get_stderr() { return stderr; }

extern FILE *get_OUTFILE() { return OUTFILE; }
extern FILE *get_ERRFILE() { return ERRFILE; }

extern void set_OUTFILE(FILE *fp) { OUTFILE_VAR=fp; }
extern void set_ERRFILE(FILE *fp) { ERRFILE_VAR=fp; }

extern LTV *brl(FILE *fp) {
    int len; char *data=NULL;
    return (data=balanced_readline(fp,&len))?LTV_init(NEW(LTV),data,len,LT_OWN):NULL;
}

extern void vm_try(int exp) { if (exp) vm_throw(LTV_NULL); }

extern FILE *file_open(char *filename,char *opts) { return fopen(filename,opts); }
extern void file_close(FILE *fp) { fclose(fp); }
extern void pinglib(char *filename)
{
    void *dlhandle=dlopen(filename,RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE | RTLD_DEEPBIND);
    if (!dlhandle)
        fprintf(OUTFILE,"dlopen error: handle %x %s\n",dlhandle,dlerror());
    else
        dlclose(dlhandle);
}

// compiled separately because of it's use of libdwelf, which conflicts with libdwarf IIRC
LTV *get_separated_debug_filename(char *filename)
{
    LTV *debug_filename=NULL;
    if ( elf_version ( EV_CURRENT ) != EV_NONE ) {
        int fd=open(filename,O_RDONLY);
        if (fd) {
            Elf *elf=elf_begin(fd,ELF_C_READ,NULL);
            if (elf) {
                GElf_Word crc;
                const void *buildid;
                const char *debuglink=dwelf_elf_gnu_debuglink(elf,&crc);
                if (debuglink) {
                    ssize_t idlen=dwelf_elf_gnu_build_id(elf,&buildid);
                    Dwarf *dwarf=dwarf_begin_elf(elf,DWARF_C_READ,NULL);
                    if (dwarf) {
                        const char *altname;
                        int idlen=dwelf_dwarf_gnu_debugaltlink(dwarf,&altname,&buildid);
                        dwarf_end(dwarf);
                    }
                    debug_filename=LTV_init(NEW(LTV),mymalloc(256),256,LT_OWN);
                    char *buf=(char *) debug_filename->data;
                    buf+=sprintf(buf,"/usr/lib/debug/.build-id/%02x",*((unsigned char *) buildid++));
                    buf+=sprintf(buf,"/");
                    while (--idlen)
                        buf+=sprintf(buf,"%02x",*((unsigned char *) buildid++));
                    buf+=sprintf(buf,".debug");
                    debug_filename->len=buf-(char *) (debug_filename->data);
                }
                elf_end(elf);
            }
            close(fd);
        }
    }
    return debug_filename;
}

extern LTV *null() { return LTV_NULL; }
extern void is_null(LTV *tos) { if (!(tos->flags&LT_NULL)) throw(LTV_NULL); }

extern void int_iszero(int a)       { if (a) throw(LTV_NULL);    }
extern void int_iseq(int a,int b)   { if (a!=b) throw(LTV_NULL); }
extern void int_isneq(int a,int b)  { if (a==b) throw(LTV_NULL); }
extern void int_islt(int a,int b)   { if (!(a<b)) throw(LTV_NULL); }
extern void int_isgt(int a,int b)   { if (!(a>b)) throw(LTV_NULL); }
extern void int_islteq(int a,int b) { if (a>b) throw(LTV_NULL); }
extern void int_isgteq(int a,int b) { if (a<b) throw(LTV_NULL); }

extern int int_add(int a,int b) { return a+b; }
extern int int_mul(int a,int b) { return a*b; }
extern int int_inc(int a) { return ++a; }
extern int int_dec(int a) { return --a; }

extern int int_to_ascii(int i) { fprintf(OUTFILE,"%c\n",i); }

int benchint=0;
extern void bench() {
    if (--benchint<0) {
        fprintf(OUTFILE,"                                          done!\n");
        benchint=0;
        throw(LTV_NULL);
    }
 done: return;
}
