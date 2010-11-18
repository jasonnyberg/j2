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

int fstrnprint(FILE *ofile,char *str,int len)
{
    char s;
    if (len==-1)
        len=strlen(str);
    
    for (s=*str;len--;s=*++str)
        switch(s)
        {
            case '\\': fputs("\\\\",ofile); break;
            case '\t': fputs("\\t",ofile); break;
            case '\r': fputs("\\r",ofile); break;
            case '\n': fputs("\\n",ofile); break;
            case '\"': fputs("\"",ofile); break;
            default: fputc(s,ofile); break;
        }
    
    return 0;
}

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

int fnmatch_len(char *pat,char *str,int len)
{
    int result;
    char eos=pat[len];
    pat[len]=0;
    result=fnmatch(pat,str,FNM_EXTMATCH);
    pat[len]=eos;
    return result;
}
