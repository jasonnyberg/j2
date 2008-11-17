#define _GNU_SOURCE // strndupa, stpcpy
#define __USE_GNU // strndupa, stpcpy
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "util.h"
#include "dict.h"

unsigned myid=1;
ull *STRTOULL_PTR;
char *STRTOULL_TAIL;

long long *STRTOLL_PTR;
char *STRTOLL_TAIL;

static int Gmymalloc=0;
static int Gerrs;

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
        Gmymalloc+=size;
    }
    return r;
}

void myfree(void *p,int size)
{
    if (p) Gmymalloc-=size;
    free(p);
}

void *mybzero(void *buf,int size)
{
    return buf?memset(buf,0,size):NULL;
}

char *bufdup(char *buf,int len)
{
    char *newbuf = mymalloc(len+1);
    memcpy(newbuf,buf,len);
    newbuf[len]=0;
    return newbuf;
}

char *ulltostr(char *format,ull i)
{
    static char buf[1024];
    sprintf(buf,format,i);
    return buf;
}


unsigned strtou(char *str)
{
    return str? strtoul(str,NULL,0):0;
}


char *ntostr(char *format,long double i)
{
    static char buf[1024];
    sprintf(buf,format,i);
    return buf;
}


long double strton(char *str)
{
    return str? strtold(str,NULL):0;
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


