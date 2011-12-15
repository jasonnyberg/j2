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
int Gerrs;

void try_error()
{
    Gerrs++;
}

void *mymalloc(int size)
{
    void *r=malloc(size);
    if (r)
    {
        bzero(r,size);
        Gmymalloc+=1;
    }
    //printf(CODE_RED "%d\n" CODE_RESET,Gmymalloc);
    return r;
}

void *myrealloc(void *buf, int newsize)
{
    char *r=realloc(buf,newsize);
    if (r)
        Gmymalloc+=1;
    //printf(CODE_RED "%d\n" CODE_RESET,Gmymalloc);
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
        if (buf[i+offset]=='\\') offset++,buf[i]=buf[i+offset],i++;
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
