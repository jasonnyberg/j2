/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 * Copyright (C) 2018 Jason Nyberg <jasonnyberg@gmail.com> (dual-licensed)
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

#ifdef LTTNG

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER edict

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./trace.h"

#if !defined(TRACE_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define TRACE_H

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

TRACEPOINT_EVENT(edict,
                 lookup,
                 TP_ARGS(char *,file,char *,func,void *,ltv,char *,name,int,len,int,insert),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_integer_hex(void *,ltv,ltv)
                           ctf_sequence_text(char,name,name,size_t,len)
                           ctf_integer(int,insert,insert)))

TRACEPOINT_EVENT(edict,
                 opcode,
                 TP_ARGS(char *,file,char *,func,int,state),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_integer(int,state,state)))
TRACEPOINT_EVENT(edict,
                 opext,
                 TP_ARGS(char *,file,char *,func,char *,opcode,int,len,int,flags,int,state),
                 TP_FIELDS(ctf_string(file,file)
                           ctf_string(func,func)
                           ctf_sequence_text(char,opcode,opcode,size_t,len)
                           ctf_integer(int,flags,flags)
                           ctf_integer(int,state,state)))


TRACEPOINT_LOGLEVEL(edict,start,    TRACE_DEBUG)
TRACEPOINT_LOGLEVEL(edict,finish,   TRACE_DEBUG)
TRACEPOINT_LOGLEVEL(edict,event,    TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,event_len,TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,event_hex,TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,error,    TRACE_ERR)
TRACEPOINT_LOGLEVEL(edict,alloc,    TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,dealloc,  TRACE_DEBUG)
TRACEPOINT_LOGLEVEL(edict,lookup,   TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,opcode,   TRACE_DEBUG_LINE)
TRACEPOINT_LOGLEVEL(edict,opext,    TRACE_DEBUG_LINE)

#endif /* TRACE_H */

#include <lttng/tracepoint-event.h>
#define TSTART(status,msg)           tracepoint(edict,start,     (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TFINISH(status,msg)          tracepoint(edict,finish,    (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TEVENT(status,msg)           tracepoint(edict,event,     (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TEVLEN(status,msg,msglen)    tracepoint(edict,event_len, (char *) __FILE__,(char *) __FUNCTION__,msg,msglen,status)
#define TEVHEX(status,data,len)      tracepoint(edict,event_hex, (char *) __FILE__,(char *) __FUNCTION__,(unsigned char *) data,len,status)
#define TERROR(status,msg)           tracepoint(edict,error,     (char *) __FILE__,(char *) __FUNCTION__,msg,status)
#define TALLOC(ptr,size,msg)         tracepoint(edict,alloc,     (char *) __FILE__,(char *) __FUNCTION__,msg,ptr,size)
#define TDEALLOC(ptr,msg)            tracepoint(edict,dealloc,   (char *) __FILE__,(char *) __FUNCTION__,msg,ptr)
#define TLOOKUP(ltv,name,len,insert) tracepoint(edict,lookup,    (char *) __FILE__,(char *) __FUNCTION__,(void *) ltv,(char *) name,len,insert)
#define TOPCODE(state)               tracepoint(edict,opcode,    (char *) __FILE__,(char *) __FUNCTION__,state)
#define TOPEXT(ext,len,flags,state)  tracepoint(edict,opext,     (char *) __FILE__,(char *) __FUNCTION__,(char *) ext,len,flags,state)

#else /* LTTNG */
#define TSTART(status,msg)
#define TFINISH(status,msg)
#define TEVENT(status,msg)
#define TEVLEN(status,msg,msglen)
#define TEVHEX(status,data,len)
#define TERROR(status,msg)
#define TALLOC(ptr,size,msg)
#define TDEALLOC(ptr,msg)
#define TLOOKUP(ltv,name,len,insert)
#define TOPCODE(opcode,state)
#define TOPEXT(ext,len,flags,state)
#endif /* LTTNG */
