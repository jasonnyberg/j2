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

#define _GNU_SOURCE // strndupa, stpcpy
#define __USE_GNU // strndupa, stpcpy
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "util.h"

#include "trace.h" // lttng

int Gmymalloc=0;

int try_depth=0;
int try_loglev=2;
int try_infolev=1;
int try_edepth=2;

__thread TRY_CONTEXT _try_context = {};
TRY_CONTEXT *        try_context() { return &_try_context; }

void try_seterr(int eid,const char *estr) {
    char str[TRY_STRLEN];

    if (_try_context.edepth++<try_edepth) {
        if (!_try_context.eid) {
            _try_context.eid=eid;
            snprintf(_try_context.errstr,TRY_STRLEN,"Failed while %s",estr);
        } else {
            snprintf(str,TRY_STRLEN,"%s, while %s",_try_context.errstr,estr);
            snprintf(_try_context.errstr,TRY_STRLEN,"%s",str);
        }
    }
}

void try_loginfo(const char *func,const char *cond) {
    char logstr[TRY_STRLEN];
    int indent=_try_context.depth;
    if (indent<0) indent=0;
    if (indent>TRY_STRLEN-1) indent=TRY_STRLEN-1;

    memset(logstr,' ',TRY_STRLEN);
    switch (try_infolev) {
        case 3: snprintf(logstr+indent,TRY_STRLEN,"%s:%s:" CODE_UL "%s",func,cond,_try_context.msgstr); break;
        case 2: snprintf(logstr+indent,TRY_STRLEN,"%s:" CODE_UL "%s",func,_try_context.msgstr); break;
        case 1: snprintf(logstr+indent,TRY_STRLEN,"%s",_try_context.msgstr); break;
        case 0: snprintf(logstr+indent,TRY_STRLEN,"%s",""); break;
    }

    fprintf(stderr,CODE_GREEN "%s" CODE_RESET NEWLINE,logstr);
    fflush(stderr);
}

void try_logerror(const char *func,const char *cond,int status) {
    char errstr[TRY_STRLEN];
    switch (try_loglev) {
        case 3: snprintf(errstr,TRY_STRLEN,"%s:%s:Failed to %s",func,cond,_try_context.msgstr); break;
        case 2: snprintf(errstr,TRY_STRLEN,"%s:Failed to %s",func,_try_context.msgstr); break;
        case 1: snprintf(errstr,TRY_STRLEN,"Failed to %s",_try_context.msgstr); break;
        case 0: snprintf(errstr,TRY_STRLEN,"%s",""); break;
    }

    fprintf(stderr,CODE_RED "%s" CODE_RESET NEWLINE,errstr);
    fflush(stderr);
}

void try_reset() {
    _try_context.eid=0;
    _try_context.edepth=0;
    _try_context.msgstr[0]=0;
    _try_context.errstr[0]=0;
}


void *mymalloc(int size) {
    void *r=calloc(size,1);
    if (r) Gmymalloc+=1;
    TALLOC(r,size,"");
    return r;
}

void *myrealloc(void *buf, int newsize) {
    void *r=realloc(buf,newsize);
    if (r) Gmymalloc+=1;
    TDEALLOC(buf,"");
    TALLOC(r,newsize,"");
    return r;
}

void myfree(void *p,int size) {
    if (p) Gmymalloc-=1;
    free(p);
    TDEALLOC(p,"");
}

void *mybzero(void *buf,int size) { return buf?memset(buf,0,size):NULL; }

char *strstrip(char *buf,int *len) {
    int i,offset;
    for (i=offset=0;(i+offset)<(*len);i++) {
        if (buf[i+offset]=='\\') offset++;
        if (offset) buf[i]=buf[i+offset];
    }
    (*len)-=offset;
    return buf;
}

int fstrnprint(FILE *ofile,char *str,int len) {
    char *buf;
    if (len==-1) len=strlen(str);
    if (!len) return 0;
    buf=STRIPDUPA(str,&len);
    do {
        switch(*buf) {
            case '\\': fputs("\\",ofile); break;
            case '\t': fputs("\\t",ofile); break;
            case '\r': fputs("\\r",ofile); break;
            case '\n': fputs("\\n",ofile); break;
            case '"': fputs("\\\"",ofile); break;
            default: fputc(*buf,ofile); break;
        }
    } while (++buf,--len);

    return len;
}

char *bufdup(const char *buf,int len) {
    // always null terminate whether string or not
    void *newbuf;
    if (len<0) len=strlen((char *) buf);
    newbuf=mymalloc(len+1);
    memcpy(newbuf,buf,len);
    ((char *) newbuf)[len]=0;
    return (char *) newbuf;
}

char *stripdup(char *buf,int *len) { return strstrip(bufdup(buf,*len),len); }

int strtou(char *str,int len,unsigned *val) {
    char *tail;
    if (!str) return 0;
    *val=strtoul(str,&tail,0);
    return str+len==tail;
}

int strton(char *str,int len,long double *val) {
    char *tail;
    if (!str) return 0;
    *val=strtold(str,&tail);
    return str+len==tail;
}

int strnncmp(char *a,int alen,char *b,int blen) {
    int mismatch;
    alen=(alen<0)?(int) strlen(a):alen;
    blen=(blen<0)?(int) strlen(b):blen;
    mismatch=strncmp(a,b,MIN(alen,blen)); // strnmatch for optimizations...

    return mismatch?mismatch:alen-blen; // if !mismatch, then longer > shorter (any char is greater than "virtual null")
}

int strnspn(char *str,int len,char *accept) {
    int result;
    char eos=str[len];
    str[len]=0;
    result=strspn(str,accept);
    str[len]=eos;
    return result;
}

int strncspn(char *str,int len,char *reject) {
    int result;
    char eos=str[len];
    str[len]=0;
    result=strcspn(str,reject);
    str[len]=eos;
    return result;
}

int fnmatch_len(char *pat,int plen,char *str,int slen) {
    int result;
    if (plen==-1)
        plen=strlen(pat);
    if (slen==-1)
        slen=strlen(str);
    char peos=pat[plen],seos=str[slen];
    pat[plen]=str[slen]=0;
    result=fnmatch(pat,str,FNM_EXTMATCH);
    pat[plen]=peos,str[slen]=seos;
    return result;
}

int shexdump(FILE *ofile,char *buf,int size,int width,int opts) {
    int i=0;
    int o(int i) { return opts&SHEXDUMP_OPT_REVERSE?(size-1-i):i; } // reversible offset
    int pad=!(opts&SHEXDUMP_OPT_UNPADDED);
    const char *sep=opts&SHEXDUMP_OPT_NOSPACE?"":" ";
    int shexbyte(int c) { return fprintf(ofile,pad?"%s%02hhx%s" CODE_RESET:"%s%2hhx%s" CODE_RESET,(c?CODE_RED:""),c,sep); }
    void readable(int j) { for (;j && o(i-j)<size;j--) fprintf(ofile,"%c",(buf[o(i-j)]<32 || buf[o(i-j)]>126)?'.':buf[o(i-j)]); }
    void hex(int j) { for (;j--;i++) o(i)<size? shexbyte(buf[o(i)]):fprintf(ofile,"  %s",sep); }

    while (i<size) fprintf(ofile,"%8d: ",i),hex(width),readable(width),fprintf(ofile,"\n");
    return size;
}

int hexdump(FILE *ofile, char *buf, int size) { return shexdump(ofile, buf, size, 16, 0); }


int escape_buf(char *buf,int len) {
    char *src = buf;
    int escape = 0;

    int hex(char c) { // return -1 if not a hex digit, else hex value
        return (c >= 48 && c <= 57)?  c-48: /* 0-9 */
               (c >= 65 && c <= 70)?  c-55: /* A-F */
               (c >= 97 && c <= 102)? c-87: /* a-f */
               -1;
    }

    for (int i = 0; i < len; i++) {
        if (escape) {
            switch(src[i]) {
                case '\"':  *buf++ = '\"'; break;
                case '/': *buf++ = '/'; break;
                case '\\': *buf++ = '\\'; break;
                case 'b': *buf++ = '\b'; break;
                case 'f': *buf++ = '\f'; break;
                case 'r': *buf++ = '\r'; break;
                case 'n': *buf++ = '\n'; break;
                case 't': *buf++ = '\t'; break;
                case 'u':
                    if (((len-i)<4) || hex(src[i+1])<0 || hex(src[i+2])<0 || hex(src[i+3])<0 || hex(src[i+4])<0) {
                        *buf++ = 'u'; // not a valid 16b hex value, just leave it as is
                        break;
                    } else {
                        *buf++ = hex(src[i++]) << 4 + hex(src[i++]);
                        *buf++ = hex(src[i++]) << 4 + hex(src[i++]);
                    }
                }
            escape = 0;
            continue;
        }
        if (src[i]=='\\')
            escape = 1;
        else
            *buf++ = src[i];
    }
}

// sequence of include chars, then sequence of not-exclude chars, then terminate balanced sequence
int series(char *buf,int len,char *include,char *exclude,char *balance) {
    int inclen=include?strlen(include):0;
    int exclen=exclude?strlen(exclude):0;
    int ballen=balance?strlen(balance)/2:0;
    int i=0,depth=0;
    if (len==-1)
        len=strlen(buf);
    int checkbal(int match) {
        if (balance) {
            int minlen=MIN(len-i,ballen);
            if (depth && !strncmp(buf+i,balance+ballen,minlen)) depth--,i+=ballen; // prefer close over open for bal[0]==bal[1]
            else if     (!strncmp(buf+i,balance,minlen))        depth++,i+=ballen;
            else if     (depth || !match)                       i++;
        } else if (!match) i++;
        return !depth && match;
    }
    if (include) while (i<len) if (buf[i]=='\\') i+=2; else if (checkbal(!memchr(include,buf[i],inclen)?1:0)) break;
    if (exclude) while (i<len) if (buf[i]=='\\') i+=2; else if (checkbal(memchr(exclude,buf[i],exclen)?1:0))  break;
    if (balance) while (i<len) if (buf[i]=='\\') i+=2; else if (checkbal(1)) break;
    return i;
}

int balanced_string(char *line, int linelen, char *open, char *close, int *length /*in:out*/, char *stack /*in:out*/) {
    int depth = strlen(stack);
    int dels = strlen(open);
    int i;
    char *pos;
    for (i = 0; i < linelen; i++, (*length)++) {
        char c = line[*length];
        if (c == '\\') { // don't interpret next char
            i++;
            (*length)++;
        } else if (pos = memchr(close, c, dels)) {
            if (depth && c == stack[depth - 1]) // balanced, pop
                stack[--depth]=0;
            else {
                fprintf(stderr, "Sequence misbalanced at \"%c\", offset %d, stack %s\n", c, *length,stack);
                *length = -1;
                break;
            }
        } else if (pos = memchr(open, c, dels)) {
            stack[depth++] = close[pos - open];
        }
    }
    return depth;
}

char *balanced_readline(FILE *ifile, char *open, char *close, int *length /*in:out*/) {
    char *line = NULL;
    char *nextline(int *linelen) {
        static char *buf=NULL;
        static size_t buflen=0;

        if ((*linelen=getline(&buf,&buflen,ifile))>0) {
            if ((line=(char *) realloc(line,(*length)+(*linelen)+1)))
                memmove(line+(*length),buf,(*linelen)+1);
            return line;
        }
        return (char *) NULL;
    }

    char stack[1024] = {}; // balancing stack
    int linelen=0;

    *length=0;
    while (nextline(&linelen) && balanced_string(line, linelen, open, close, length, stack) && *length>=0);
    if (*length<=0)
        DELETE(line);
    return line; // length<0 signifies error!
}
