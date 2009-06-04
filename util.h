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

static int RESULT;

typedef unsigned long long ull;

extern void try_error();

static int status;

/** run sequential steps without nesting, with error reporting, and with support for unrolling */
#define TRY(cond,fail_status,exitpoint,args...)                                                                          \
    {                                                                                                                    \
        if ((cond))                                                                                                      \
        {                                                                                                                \
            status = (int) fail_status;                                                                                   \
            try_error();                                                                                                 \
            printf(CODE_RED "TRY_ERR in %s: " #cond "=%d: Jumping to " #exitpoint ": ",__func__,status);                 \
            printf(args); printf(CODE_RESET);                                                                            \
            goto exitpoint;                                                                                              \
        }                                                                                                                \
    }

/** A version of TRY that also reports what it's doing as well as just errors */
#define TRYLOG(cond,fail_status,exitpoint,args...) { printf("%s: " #cond "\n",__func__); TRY(cond,fail_status,exitpoint,args) }

#define SETENUM(type,var,val) { if (validate_##type(val) var=(type) (val); else { printf(CODE_RED "Invalid value: select from: " CODE_RESET "\n"); list_##type(); }

#define FORMATA(p,len,fmt,args...) (p=alloca(strlen(fmt)+len+1),sprintf(p,fmt,args),p)
#define CONCATA(p,str1,str2) (stpcpy(stpcpy((p=alloca(strlen(str1)+strlen(str2)+1)),(str1)),(str2)),p)

extern void *mymalloc(int size);
extern void myfree(void *p,int size);

extern void *mybzero(void *p,int size);
#define ZERO(x) (*(typeof(&x))mybzero(&x,sizeof(x)))

#define NEW(type) (mybzero(mymalloc(sizeof(type)),sizeof(type)))
#define DELETE(var) (myfree(var,0))
#define RELEASE(var) (DELETE(var),var=NULL)

extern char *bufdup(char *buf,int len);

extern char *ulltostr(char *format,ull i);
extern unsigned strtou(char *str);

extern char *ntostr(char *format,long double i);
extern long double strton(char *str);

extern int fnmatch_len(char *pat,char *str,int len);

extern ull *STRTOULL_PTR;
extern char *STRTOULL_TAIL;

extern long long *STRTOLL_PTR;
extern char *STRTOLL_TAIL;

extern void **STRTOPTR_PTR;
extern char *STRTOPTR_TAIL;

// returns a pointer to ULL if valid number, NULL otherwise
#define STRTOPTRP(str)                                                 \
    (                                                                  \
     str? (                                                            \
           (STRTOPTR_PTR = alloca(sizeof(*STRTOPTR_PTR))),             \
           (*STRTOPTR_PTR = strtoull(str,&STRTOPTR_TAIL,0)),           \
           ((str == STRTOPTR_TAIL)? NULL:STRTOPTR_PTR)                 \
           ) : NULL                                                    \
     )

// returns a pointer to ULL if valid number, NULL otherwise
#define STRTOULLP(str)                                                 \
    (                                                                  \
     str? (                                                            \
           (STRTOULL_PTR = alloca(sizeof(*STRTOULL_PTR))),             \
           (*STRTOULL_PTR = strtoull(str,&STRTOULL_TAIL,0)),           \
           ((str == STRTOULL_TAIL)? NULL:STRTOULL_PTR)                 \
           ) : NULL                                                    \
     )

// returns a pointer to ULL if valid number, NULL otherwise
#define STRTOLLP(str)                                                  \
    (                                                                  \
     str? (                                                            \
           (STRTOLL_PTR = alloca(sizeof(*STRTOLL_PTR))),               \
           (*STRTOLL_PTR = strtoll(str,&STRTOLL_TAIL,0)),              \
           ((str == STRTOLL_TAIL)? NULL:STRTOLL_PTR)                   \
           ) : NULL                                                    \
     )


#endif

