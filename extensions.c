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


#define _GNU_SOURCE
#define _C99
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h> // dlopen/dlsym/dlclose

#include "util.h"
#include "listree.h"
#include "extensions.h"

extern int square(int a) { return a*a; }
extern int minus(int a,int b) { return a-b; }
extern int string(char *s) { printf("%s\n",s); }
extern LTV *ltv_coersion_test(LTV *ltv) { print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0); return ltv; } // no change to stack means success

extern FILE *get_stdin() { return stdin; }
extern LTV *brl(FILE *fp) {
    int len; char *data=NULL;
    return (data=balanced_readline(fp,&len))?LTV_init(NEW(LTV),data,len,LT_OWN):NULL;
}

extern void throw(LTV *ltv) { vm_throw(ltv); }
extern void try(int exp) { if (exp) vm_throw(LTV_NULL); }

extern FILE *file_open(char *filename,char *opts) { return fopen(filename,opts); }
extern void file_close(FILE *fp) { fclose(fp); }
extern void pinglib(char *filename)
{
    void *dlhandle=dlopen(filename,RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE | RTLD_DEEPBIND);
    if (!dlhandle)
        printf("dlopen error: handle %x %s\n",dlhandle,dlerror());
    else
        dlclose(dlhandle);
}

extern LTV *null() { return LTV_NULL; }
extern void is_null(LTV *tos) { if (!(tos->flags&LT_NULL)) throw(LTV_NULL); }

extern void int_iszero(int a) { if (a) throw(LTV_NULL); }
extern void int_iseq(int a,int b) { if (a!=b) throw(LTV_NULL); }
extern void int_isneq(int a,int b) { if (a==b) throw(LTV_NULL); }
extern int int_add(int a,int b) { return a+b; }
extern int int_mul(int a,int b) { return a*b; }

int benchint=0;
extern void bench() {
    if (++benchint==100000) {
        printf("                                          done!\n");
        benchint=0;
        throw(LTV_NULL);
    }
 done: return;
}
