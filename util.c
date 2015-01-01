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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "util.h"

unsigned myid=1;
ull *STRTOULL_PTR;
char *STRTOULL_TAIL;

long long *STRTOLL_PTR;
char *STRTOLL_TAIL;

int Gmymalloc=0;

int try_depth=1;
int try_loglev=1;
int try_infolev=1;
int try_edepth=2;

__thread TRY_CONTEXT try_context;

void try_seterr(int eid,const char *estr)
{
    char str[TRY_STRLEN];

    if (try_context.edepth++<try_edepth)
    {
        if (!try_context.eid)
        {
            try_context.eid=eid;
            snprintf(try_context.errstr,TRY_STRLEN,"Failed while %s",estr);
        }
        else
        {
            snprintf(str,TRY_STRLEN,"%s, while %s",try_context.errstr,estr);
            snprintf(try_context.errstr,TRY_STRLEN,"%s",str);
        }
    }
}

void try_loginfo(const char *func,const char *cond)
{
    char logstr[TRY_STRLEN];
    int indent=try_context.depth;
    if (indent<0) indent=0;
    if (indent>TRY_STRLEN-1) indent=TRY_STRLEN-1;

    memset(logstr,' ',TRY_STRLEN);
    switch (try_infolev)
    {
        case 3: snprintf(logstr+indent,TRY_STRLEN,"%s:%s:" CODE_UL "%s",func,cond,try_context.msgstr); break;
        case 2: snprintf(logstr+indent,TRY_STRLEN,"%s:" CODE_UL "%s",func,try_context.msgstr); break;
        case 1: snprintf(logstr+indent,TRY_STRLEN,"%s",try_context.msgstr); break;
        case 0: snprintf(logstr+indent,TRY_STRLEN,"%s",""); break;
    }

    fprintf(stderr,CODE_GREEN "%s" CODE_RESET NEWLINE,logstr); // prints to stdout!
    fflush(stdout);
}

void try_logerror(const char *func,const char *cond,int status)
{
    char errstr[TRY_STRLEN];
    switch (try_loglev)
    {
        case 3: snprintf(errstr,TRY_STRLEN,"%s:%s:Failed while %s",func,cond,try_context.msgstr); break;
        case 2: snprintf(errstr,TRY_STRLEN,"%s:Failed while %s",func,try_context.msgstr); break;
        case 1: snprintf(errstr,TRY_STRLEN,"Failed while %s",try_context.msgstr); break;
        case 0: snprintf(errstr,TRY_STRLEN,"%s",""); break;
    }

    fprintf(stderr,CODE_RED "%s" CODE_RESET NEWLINE,errstr); // prints to stdout!
    fflush(stdout);
}

void try_reset(int context)
{
    try_context.eid=0;
    try_context.edepth=0;
    try_context.msgstr[0]=0;
    try_context.errstr[0]=0;
}


void *mymalloc(int size)
{
    void *r=calloc(size,1);
    if (r) Gmymalloc+=1;
    return r;
}

void *myrealloc(void *buf, int newsize)
{
    char *r=realloc(buf,newsize);
    if (r) Gmymalloc+=1;
    return r;
}

void myfree(void *p,int size)
{
    if (p) Gmymalloc-=1;
    free(p);
}

void *mybzero(void *buf,int size)
{
    return buf?memset(buf,0,size):NULL;
}

char *strstrip(char *buf,int *len)
{
    int i,offset;
    for (i=offset=0;(i+offset)<(*len);i++)
    {
        if (buf[i+offset]=='\\') offset++;
        if (offset) buf[i]=buf[i+offset];
    }
    (*len)-=offset;
    return buf;
}

int fstrnprint(FILE *ofile,char *str,int len)
{
    char *buf;
    if (len==-1) len=strlen(str);
    if (!len) return 0;
    buf=STRIPDUPA(str,&len);
    do
    {
        switch(*buf)
        {
            case '\\': fputs("\\",ofile); break;
            case '\t': fputs("\\t",ofile); break;
            case '\r': fputs("\\r",ofile); break;
            case '\n': fputs("\\n",ofile); break;
            case '\"': fputs("\"",ofile); break;
            default: fputc(*buf,ofile); break;
        }
    } while (++buf,--len);

    return len;
}

char *bufdup(char *buf,int len)
{
    // always null terminate whether string or not
    char *newbuf;
    if (len<0) len=strlen(buf);
    newbuf=mymalloc(len+1);
    memcpy(newbuf,buf,len);
    newbuf[len]=0;
    return newbuf;
}

char *stripdup(char *buf,int *len)
{
    return strstrip(bufdup(buf,*len),len);
}

int strtou(char *str,int len,unsigned *val)
{
    char *tail;
    if (!str) return 0;
    *val=strtoul(str,&tail,0);
    return str+len==tail;
}

int strton(char *str,int len,long double *val)
{
    char *tail;
    if (!str) return 0;
    *val=strtold(str,&tail);
    return str+len==tail;
}

int strnncmp(char *a,int alen,char *b,int blen)
{
    int mismatch;
    alen=(alen<0)?strlen(a):alen;
    blen=(blen<0)?strlen(b):blen;
    mismatch=strncmp(a,b,MIN(alen,blen));

    return mismatch?mismatch:alen-blen;
}

int strnspn(char *str,int len,char *accept)
{
    int result;
    char eos=str[len];
    str[len]=0;
    result=strspn(str,accept);
    str[len]=eos;
    return result;
}

int strncspn(char *str,int len,char *reject)
{
    int result;
    char eos=str[len];
    str[len]=0;
    result=strcspn(str,reject);
    str[len]=eos;
    return result;
}

int fnmatch_len(char *pat,int plen,char *str,int slen)
{
    int result;
    char peos=pat[plen],seos=str[slen];
    pat[plen]=str[slen]=0;
    result=fnmatch(pat,str,FNM_EXTMATCH);
    pat[plen]=peos,str[slen]=seos;
    return result;
}

int shexdump(char *buf,int size,int width,int opts)
{
    int i=0;
    int o(int i) { return opts&SHEXDUMP_OPT_REVERSE?(size-1-i):i; } // reversible offset
    int pad=!(opts&SHEXDUMP_OPT_UNPADDED);
    char *sep=opts&SHEXDUMP_OPT_NOSPACE?"":" ";
    int shexbyte(int c) { return printf(pad?"%s%02hhx%s" CODE_RESET:"%s%2hhx%s" CODE_RESET,(c?CODE_RED:""),c,sep); }
    void readable(int j) { for (;j && o(i-j)<size;j--) printf("%c",(buf[o(i-j)]<32 || buf[o(i-j)]>126)?'.':buf[o(i-j)]); }
    void hex(int j) { for (;j--;i++) o(i)<size? shexbyte(buf[o(i)]):printf("  %s",sep); }

    while (i<size) printf("%8d: ",i),hex(width),readable(width),printf("\n");
    return size;
}

int hexdump(char *buf,int size) { return shexdump(buf,size,16,0); }

// sequence of include chars, then sequence of not-exclude chars, then terminate balanced sequence
int series(char *buf,int len,char *include,char *exclude,char *balance) {
    int inclen=include?strlen(include):0;
    int exclen=exclude?strlen(exclude):0;
    int ballen=balance?strlen(balance)/2:0;
    int i=0,depth=0;
    int checkbal() {
        int minlen=MIN(len-i,ballen);
        if      (!strncmp(buf+i,balance,minlen))        depth++,i+=ballen;
        else if (!strncmp(buf+i,balance+ballen,minlen)) depth--,i+=ballen;
        else i+=depth?1:0;
        return depth;
    }
    if (include) for (;i<len;i++) if (buf[i]=='\\') i++; else if (!memchr(include,buf[i],inclen)) break;
    if (exclude) for (;i<len;i++) if (buf[i]=='\\') i++; else if (memchr(exclude,buf[i],exclen)) break;
    if (balance) for (;i<len;)    if (buf[i]=='\\') i++; else if (checkbal()==0) break;
    return i;
}

char *balanced_readline(FILE *ifile,int *length) {
     char *expr=NULL;

     char *nextline(int *linelen) {
         static char *line=NULL;
         static size_t buflen=0;

         if ((*linelen=getline(&line,&buflen,ifile))>0)
         {
             if ((expr=realloc(expr,(*length)+(*linelen)+1)))
                 memmove(expr+(*length),line,(*linelen)+1);
             return expr;
         }
         return NULL;
     }

     int depth=0;
     char delimiter[1024]; // balancing stack
     int linelen=0;

     *length=0;

     while (nextline(&linelen))
     {
         int i;
         for (i=0;i<linelen;i++,(*length)++)
         {
             switch(expr[*length])
             {
                 case '\\': i++; (*length)++; break; // don't interpret next char
                 case '(': delimiter[++depth]=')'; break;
                 case '[': delimiter[++depth]=']'; break;
                 case '{': delimiter[++depth]='}'; break;
                 case '<': delimiter[++depth]='>'; break;
                 case ')': case ']': case '}': case '>':
                     if (depth)
                     {
                         if (expr[*length]==delimiter[depth]) depth--;
                         else
                         {
                             printf("ERROR: Sequence unbalanced at \"%c\", offset %d\n",expr[*length],*length);
                             free(expr); expr=NULL;
                             *length=depth=0;
                             goto done;
                         }
                     }
                     break;
                 default: break;
             }
         }
         if (!depth)
             break;
     }

     done:
     return (*length && !depth)?expr:(free(expr),NULL);
 }


