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


#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <string.h>

//#define mymalloc(len) mymalloc2((len),(__FILE__),(__LINE__))
//#define myfree(p) myfree2((p),(__FILE__),(__LINE__))

#define WHITESPACE " \t\n"
#define NEWLINE "\r\n"


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

#define false 0
#define true !false

//#define PEDANTIC(alt,args...) args
#define PEDANTIC(alt,args...) alt

#define CAR(first,rest...) first
#define CDR(first,rest...) rest

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#define NON_NULL (NULL-1)

extern int Gmymalloc;

static inline int minint(int a,int b) { return MIN(a,b); }
static inline int maxint(int a,int b) { return MAX(a,b); }

extern int try_depth;
extern int try_loglev;
extern int try_infolev;
extern int try_edepth;

#define TRY_STRLEN 1024
typedef char TRY_STRING[TRY_STRLEN];

typedef struct TRY_CONTEXT
{
    int depth;
    int edepth;
    int eid;
    TRY_STRING msgstr;
    TRY_STRING errstr;
} TRY_CONTEXT;

extern __thread TRY_CONTEXT try_context;

extern int try_init();
extern void try_seterr(int eid,const char *estr);
extern void try_reset();
extern void try_loginfo(const char *func,const char *cond);
extern void try_logerror(const char *func,const char *cond,int status);

#define TRY_ERR -1

/** run sequential steps without nesting, with error reporting, and with support for unrolling */
#pragma GCC diagnostic ignored "-Wformat-zero-length"
#define TRY(_cond_,_msg_...)                                            \
    do {                                                                \
        if (try_context.depth<try_depth)                                \
        {                                                               \
            snprintf(try_context.msgstr,TRY_STRLEN,_msg_);              \
            try_loginfo(__func__,#_cond_);                              \
        }                                                               \
        try_context.depth++;                                            \
        status=(_cond_);                                                \
        try_context.depth--;                                            \
    } while (0)

#define CATCH(_cond_,_fail_status_,_todo_,_msg_...)                     \
    do {                                                                \
        if (_cond_)                                                     \
        {                                                               \
            status=(_fail_status_);                                     \
            if (status)                                                 \
            {                                                           \
                snprintf(try_context.msgstr,TRY_STRLEN,_msg_);          \
                try_seterr((int) status,try_context.msgstr);            \
                try_logerror((__func__),#_cond_,(int) status);          \
            }                                                           \
            else                                                        \
            {                                                           \
                try_reset();                                            \
            }                                                           \
            _todo_;                                                     \
        }                                                               \
    } while (0)

#define SCATCH(_msg_...) CATCH(status!=0,status,goto done,_msg_);

#define TRYCATCH(_cond_,_fail_status_,_exit_,_msg_...) do { TRY(_cond_,_msg_); CATCH(status!=0,_fail_status_,goto _exit_,_msg_); } while (0)
#define STRY(_cond_,_msg_...) TRYCATCH(_cond_,status,done,_msg_)

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

extern char *bufdup(const char *buf,int len);
extern char *stripdup(char *buf,int *len);

extern int strtou(char *str,int len,unsigned *val);
extern int strton(char *str,int len,long double *val);

extern int strnncmp(char *a,int alen,char *b,int blen);
extern int strnspn(char *str,int len,char *accept);
extern int strncspn(char *str,int len,char *reject);
extern int fnmatch_len(char *pat,int plen,char *str,int slen);

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE !FALSE
#endif

/*********************************************************************/
/* Macros implementing a linked-list based stack, where list head is same type as list element.
 * Given any "struct X" that has a self-referencing "struct X *next" member, these macros can
 * manage push, pop, and traversal operations.
 * "head": A pointer to a "struct X" object acting as the head of a list.
 * "iter": A pointer to a "struct X" object that trails one node behind the "active" node during a traversal.
 * "elem": A pointer to a "struct X" object that is to be pushed onto a stack.
 *
 * Example uses: reverse a stack's elements:
 *
 * main()
 * {
 *     int i;
 *     struct XXX { struct XXX *next; int i; } head,node[100],*item,*iter;
 *     memset(&head,0,sizeof(head));
 *     memset(&node,0,sizeof(node));
 *
 *     for (i=0;i<20;i++)
 *         node[i].i=i, STACK_PUSH(&head,&node[i]); // add 20 nodes to a stack/list
 *
 *     for(item=STACK_NEWITER(iter,&head);item;item=STACK_ITERATE(iter))
 *         printf("%d\n",item->i); // display the nodes
 *
 *     while(item=STACK_NEWITER(iter,&head)) // don't iterate while popping, just reset iter
 *         STACK_POP(iter); // pop the nodes off of the list
 * }
 */
#define STACK_NEWITER(iter,head) (((iter)=(__typeof__(iter))(head))?(iter)->next:NULL) ///< setup an iterator, return first list item
#define STACK_ITERATE(iter)      (((iter) && ((iter)=(iter)->next))?(iter)->next:NULL) ///< step iterator, return next list item
#define STACK_PUSH(iter,item)    ((iter) && (item) && ((item)->next=(iter)->next, (iter)->next=(item))) ///< push item onto stack, returning true on success and false on failure
#define STACK_POP(iter)          ((iter) && ((!(iter)->next) || (((iter)->next=(iter)->next->next),TRUE))) ///< pop item associated with iter off of stack returning true on success and false on failure


#define SHEXDUMP_OPT_UNPADDED 0x1
#define SHEXDUMP_OPT_REVERSE  0x2
#define SHEXDUMP_OPT_NOSPACE  0x4

extern int shexdump(FILE *ofile,char *buf,int size,int width,int opts);
extern int hexdump(FILE *ofile,char *buf,int size);

extern int series(char *buf,int len,char *include,char *exclude,char *balance);
extern char *balanced_readline(FILE *ifile,int *length);


// Coroutine tool... wrap function with crBegin and crFinish, and use crReturn to return after saving location at which to resume execution (must not be used within a switch()
#define crBegin static int _crstate=0; switch(_crstate) { case 0:
#define crReturn(x) do { _crstate=__LINE__; return x; case __LINE__:; } while (0)
#define crFinish }
// int function(void) {
//    static int i;
//    crBegin;
//    for (i = 0; i < 10; i++)
//        crReturn(1, i);
//    crFinish;
//}

#endif
