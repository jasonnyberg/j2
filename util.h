#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <string.h>

//#define mymalloc(len) mymalloc2((len),(__FILE__),(__LINE__))
//#define myfree(p) myfree2((p),(__FILE__),(__LINE__))

#define WHITESPACE " \t\n"

#define BZERO(x) bzero((&x),sizeof(x))

#define CODE_RESET      "\e[0m"  // reset all attributes to their defaults
#define CODE_BOLD       "\e[1m"  // set bold
#define CODE_DIM        "\e[2m"  // set half-bright (simulated with color on a color display)
#define CODE_UL         "\e[4m"  // set underscore (simulated with color on a color display) (the colors used to simulate dim or underline are set using ESC ] ...)
#define CODE_BLINK      "\e[5m"  // set blink
#define CODE_RVSVID     "\e[7m"  // set reverse video
#define CODE_NOTDIM     "\e[22m" // set normal intensity
#define CODE_NOUL       "\e[24m" // underline off
#define CODE_NOBLNK     "\e[25m" // blink off
#define CODE_NORVID     "\e[27m" // reverse video off
#define CODE_BLACK      "\e[30m" // set black foreground
#define CODE_RED        "\e[31m" // set red foreground
#define CODE_GREEN      "\e[32m" // set green foreground
#define CODE_BROWN      "\e[33m" // set brown foreground
#define CODE_BLUE       "\e[34m" // set blue foreground
#define CODE_MAGEN      "\e[35m" // set magenta foreground
#define CODE_CYAN       "\e[36m" // set cyan foreground
#define CODE_WHITE      "\e[37m" // set white foreground
#define CODE_ULDEFFG    "\e[38m" // set underscore on, set default foreground color
#define CODE_NOULDEFFG  "\e[39m" // set underscore off, set default foreground color
#define CODE_BGBLACK    "\e[40m" // set black background
#define CODE_BGRED      "\e[41m" // set red background
#define CODE_BGGREEN    "\e[42m" // set green background
#define CODE_BGBROWN    "\e[43m" // set brown background
#define CODE_BGBLUE     "\e[44m" // set blue background
#define CODE_BGMAGEN    "\e[45m" // set magenta background
#define CODE_BGCYAN     "\e[46m" // set cyan background
#define CODE_BGWITE     "\e[47m" // set white background
#define CODE_BGDEFLT    "\e[49m" // set default background color


typedef unsigned long long ull;

//#define PEDANTIC(alt,args...) args
#define PEDANTIC(alt,args...) alt


#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

static inline int minint(int a,int b) { return MIN(a,b); }
static inline int maxint(int a,int b) { return MAX(a,b); }

extern void try_error();

#define FORMAT_LEN(format,args...) (strlen(format))
/** run sequential steps without nesting, with error reporting, and with support for unrolling */
#define TRY(cond,fail_status,exitpoint,args...)                                                                          \
    {                                                                                                                    \
        if ((cond))                                                                                                      \
        {                                                                                                                \
            status = (int) fail_status;                                                                                  \
            try_error();                                                                                                 \
            if (status && FORMAT_LEN(args))                                                                              \
            {                                                                                                            \
                fprintf(stderr,CODE_RED "TRY_ERR in %s: " #cond "=%d: Jumping to " #exitpoint ": ",__func__,status);     \
                fprintf(stderr,args); fprintf(stderr,CODE_RESET);                                                        \
            }                                                                                                            \
            goto exitpoint;                                                                                              \
        }                                                                                                                \
    }

/** A version of TRY that also reports what it's doing as well as just errors */
#define TRYLOG(cond,fail_status,exitpoint,args...) { fprintf(stderr,"%s: " #cond "\n",__func__); TRY(cond,fail_status,exitpoint,args) }

#define SETENUM(type,var,val) { if (validate_##type(val) var=(type) (val); else { printf(CODE_RED "Invalid value: select from: " CODE_RESET "\n"); list_##type(); }

#define FORMATA(p,len,fmt,args...) (p=alloca(strlen(fmt)+len+1),sprintf(p,fmt,args),p)
#define CONCATA(p,str1,str2) (stpcpy(stpcpy((p=alloca(strlen(str1)+strlen(str2)+1)),(str1)),(str2)),p)
#define STRIPDUPA(str,len) (strstrip(memcpy(alloca(*len),str,*len),len))

extern void *mymalloc(int size);
extern void *myrealloc(void *buf,int newsize);
extern void myfree(void *p,int size);

extern void *mybzero(void *p,int size);
#define ZERO(x) (*(typeof(&x))mybzero(&x,sizeof(x)))

#define NEW(type) (mybzero(mymalloc(sizeof(type)),sizeof(type)))
#define RENEW(var,newlen) (myrealloc(var,newlen))
#define DELETE(var) (myfree(var,0))
#define RELEASE(var) (DELETE(var),var=NULL)

extern char *strstrip(char *buf,int *len);
extern int fstrnprint(FILE *ofile,char *str,int len);

extern char *bufdup(char *buf,int len);
extern char *stripdup(char *buf,int *len);

extern int strtou(char *str,int len,unsigned *val);
extern int strton(char *str,int len,long double *val);

extern int strnncmp(char *a,int alen,char *b,int blen);
extern int strnspn(char *str,int len,char *accept);
extern int strncspn(char *str,int len,char *reject);
extern int fnmatch_len(char *pat,int plen,char *str,int slen);
#endif

