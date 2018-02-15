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

#include "util.h"
#include "listree.h"

extern int square(int a) { return a*a; }
extern int minus(int a,int b) { return a-b; }
extern int string(char *s) { printf("%s\n",s); }

extern LTV *ltv_coersion_test(LTV *ltv) { print_ltv(stdout,CODE_RED,ltv,CODE_RESET "\n",0); return ltv; }


extern void capture() { stdin; stdout; stderr; }
extern FILE *get_stdin() { return stdin; }

/*
extern LTV *read_compile(FILE *file,int format) { // temp hack until I can write this directly in edict
    int status=0;
    char *data;
    int len;
    STRY((data=balanced_readline(file,&len))==NULL,"reading balanced line from file");
    LTV *ltv=NULL;
    TRYCATCH(!(ltv=compile(compilers[format],data,len)),TRY_ERR,free_data,"compiling balanced line");
    print_ltv(stdout,"bytecodes:\n",ltv,"\n",0);
 free_data:
    DELETE(data);
 done:
    return ltv;
}
*/

extern LTV *brl(FILE *fp) {
    int len; char *data=NULL;
    return (data=balanced_readline(fp,&len))?LTV_init(NEW(LTV),data,len,LT_OWN):NULL;
}

extern FILE *file_open(char *filename,char *opts) { return fopen(filename,opts); }
extern void file_close(FILE *fp) { fclose(fp); }
