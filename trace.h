#ifdef LTTNG

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER edict

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./trace.h"

#if !defined(PSD_TP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define EDICT_TP_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(edict,
                 start,
                 TP_ARGS(char *,file,char *,func,char *,msg,int,status),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_string(msg,msg)
                           ctf_integer(int,status,status)))

TRACEPOINT_EVENT(edict,
                 finish,
                 TP_ARGS(char *,file,char *,func,char *,msg,int,status),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_string(msg,msg)
                           ctf_integer(int,status,status)))

TRACEPOINT_EVENT(edict,
                 event,
                 TP_ARGS(char *,file,char *,func,char *,msg,int,status),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_string(msg,msg)
                           ctf_integer(int,status,status)))

TRACEPOINT_EVENT(edict,
                 event_len,
                 TP_ARGS(char *,file,char *,func,char *,msg,int,msglen,int,status),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_sequence_text(char,msg,msg,size_t,msglen)
                           ctf_integer(int,status,status)))

TRACEPOINT_EVENT(edict,
                 event_hex,
                 TP_ARGS(char *,file,char *,func,unsigned char *,data,int,len,int,status),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_sequence(unsigned char,data,data,size_t,len)
                           ctf_integer(int,status,status)))

TRACEPOINT_EVENT(edict,
                 error,
                 TP_ARGS(char *,file,char *,func,char *,msg,int,status),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_string(msg,msg)
                           ctf_integer(int,status,status)))

TRACEPOINT_EVENT(edict,
                 alloc,
                 TP_ARGS(char *,file,char *,func,char *,msg,void *,ptr,int,size),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_string(msg,msg)
                           ctf_integer_hex(void *,ptr,ptr)
                           ctf_integer(int,size,size)))

TRACEPOINT_EVENT(edict,
                 dealloc,
                 TP_ARGS(char *,file,char *,func,char *,msg,void *,ptr),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_string(msg,msg)
                           ctf_integer_hex(void *,ptr,ptr)))


TRACEPOINT_LOGLEVEL(edict,start,    TRACE_DEBUG)
TRACEPOINT_LOGLEVEL(edict,finish,   TRACE_DEBUG)
TRACEPOINT_LOGLEVEL(edict,event,    TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,event_len,TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,event_hex,TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,error,    TRACE_ERR)
TRACEPOINT_LOGLEVEL(edict,alloc,    TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,dealloc,  TRACE_DEBUG)

#endif /* EDICT_TP_H */

#include <lttng/tracepoint-event.h>
#define TSTART(status,msg)        tracepoint(edict,start,     (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TFINISH(status,msg)       tracepoint(edict,finish,    (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TEVENT(status,msg)        tracepoint(edict,event,     (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TEVLEN(status,msg,msglen) tracepoint(edict,event_len, (char *) __FILE__,(char *) __FUNCTION__,msg,msglen,status)
#define TEVHEX(status,data,len)   tracepoint(edict,event_hex, (char *) __FILE__,(char *) __FUNCTION__,(unsigned char *) data,len,status)
#define TERROR(status,msg)        tracepoint(edict,error,     (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TALLOC(ptr,size,msg)      tracepoint(edict,alloc,     (char *) __FILE__,(char *) __FUNCTION__,msg,ptr,size)
#define TDEALLOC(ptr,msg)         tracepoint(edict,dealloc,   (char *) __FILE__,(char *) __FUNCTION__,msg,ptr)

#else /* LTTNG */
#define TSTART(status,msg)
#define TFINISH(status,msg)
#define TEVENT(status,msg)
#define TEVLEN(status,msg,msglen)
#define TEVHEX(status,data,len)
#define TERROR(status,msg)
#define TALLOC(ptr,size,msg)
#define TDEALLOC(ptr,msg)
#endif /* LTTNG */

