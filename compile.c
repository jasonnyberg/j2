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

#include <dlfcn.h>
#include <arpa/inet.h>

#include "util.h"
#include "compile.h"
#include "vm.h"

char *formats[] = {"asm","edict","xml","json","yaml","swagger","lisp","massoc"};

int jit_term(char *type,char *data,int len)
{
    printf("%s, \"",type);
    fstrnprint(stdout,data,len);
    printf("\"\n");
}

int jit_asm(EMITTER emit,void *data,int len)
{
    void *end=data+len;
    VM_CMD *cmd=data;
    for (int i=0;(void *)(cmd+i)<end && cmd[i].op;i++) {
        emit(cmd+i);
    }
}

#define EDICT_OPS "|&!%#@/+="
#define EDICT_MONO_OPS "()<>{}"

#define EMIT(bc) emit(&((VM_CMD) {VMOP_ ## bc}))

int jit_edict(EMITTER emit,void *data,int len)
{
    int status=0;
    char *tdata=(char *) data;
    int tlen=0;

    int advance(int adv) { adv=MIN(adv,len); tdata+=adv; len-=adv; return adv; }

    int compile_ws() {
        tlen=series(tdata,len,WHITESPACE,NULL,NULL);
        return advance(tlen);
    }

    int compile_block() {
        if ((tlen=series(tdata,len,NULL,NULL,"[]"))) {
            emit(&((VM_CMD) {VMOP_LIT,tlen-2,LT_DUP,tdata+1}));
            EMIT(SPUSH);
        } else if ((tlen=series(tdata,len,NULL,NULL,"()"))) {
            EMIT(NULL_ITEM);
            EMIT(ENFRAME);
            EMIT(SPOP);
            EMIT(EDICT);
            emit(&((VM_CMD) {VMOP_LIT,tlen-2,LT_DUP,tdata+1}));
            EMIT(EDICT);
            EMIT(YIELD);
            EMIT(DEFRAME);
        } else if ((tlen=series(tdata,len,NULL,NULL,"<>"))) {
            EMIT(ENFRAME);
            emit(&((VM_CMD) {VMOP_LIT,tlen-2,LT_DUP,tdata+1}));
            EMIT(EDICT);
            EMIT(YIELD);
            EMIT(DEFRAME);
        } else if ((tlen=series(tdata,len,NULL,NULL,"{}"))) { // just a block (for now)
            EMIT(NULL_ITEM);
            EMIT(ENFRAME);
            emit(&((VM_CMD) {VMOP_LIT,tlen-2,LT_DUP,tdata+1}));
            EMIT(EDICT);
            EMIT(YIELD);
            EMIT(DEFRAME);
        }
        return advance(tlen);
    }

    int compile_atom() {
        char *ops_data=tdata;
        int ops_len=series(tdata,len,EDICT_OPS,NULL,NULL);
        advance(ops_len);
        int ref_len=0,tlen=0;
        while ((tlen=series(tdata+ref_len,len-ref_len,NULL,NULL,"''")) || // quoted ref component
               (tlen=series(tdata+ref_len,len-ref_len,NULL,WHITESPACE EDICT_OPS EDICT_MONO_OPS "'","[]"))) // unquoted ref
                ref_len+=tlen;

        // ideally, anonymous items are treated like any other named item, w/name "$" (but merged up as frames are closed.)

        if (ref_len) {
            emit(&((VM_CMD) {VMOP_REF,ref_len,LT_DUP,tdata}));
            if (!ops_len) {
                EMIT(REF_HRES);
                EMIT(DEREF);
                EMIT(SPUSH);
            } else {
                for (int i=0;i<ops_len;i++) {
                    switch (ops_data[i]) {
                        case '#': EMIT(BUILTIN); break;
                        case '@': EMIT(SPOP); EMIT(REF_INS); EMIT(ASSIGN); break;
                        case '/': EMIT(REF_HRES); EMIT(REMOVE); break;
                        case '+': EMIT(REF_HRES); EMIT(APPEND); break;
                        case '=': EMIT(REF_HRES); EMIT(COMPARE); break;
                        case '&': EMIT(REF_HRES); EMIT(THROW); break;
                        case '|': EMIT(REF_ERES); EMIT(CATCH); break;
                        case '!': EMIT(REF_HRES); EMIT(SPOP); EMIT(MMAP_KEEP); EMIT(YIELD); break;
                        case '%': EMIT(REF_HRES); EMIT(SPOP); EMIT(MMAP_POP); EMIT(YIELD); break;
                    }
                }
            }
            EMIT(REF_KILL);
            advance(ref_len);
        } else {
            for (int i=0;i<ops_len;i++) {
                switch (ops_data[i]) {
                    case '#': EMIT(TOS); break;
                    case '@': EMIT(REF_MAKE); EMIT(REF_INS); EMIT(ASSIGN); EMIT(REF_KILL); break;
                    case '/': EMIT(SPOP); EMIT(RES_WIP); EMIT(DROP); break;
                    case '!': EMIT(SPOP); EMIT(EDICT); EMIT(YIELD); break;
                    case '&': EMIT(THROW); break;
                    case '|': EMIT(CATCH); break;
                }
            }
        }
        tlen=ops_len+ref_len;
        return tlen;
    }

    STRY(!tdata,"testing compiler source");
    while (len && (compile_ws() || compile_block() || compile_atom()));

 done:
    return status;
}

int jit_xml(EMITTER emit,void *data,int len)     { printf("unsupported\n"); }
int jit_json(EMITTER emit,void *data,int len)    { printf("unsupported\n"); }
int jit_yaml(EMITTER emit,void *data,int len)    { printf("unsupported\n"); }
int jit_swagger(EMITTER emit,void *data,int len) { printf("unsupported\n"); }
int jit_lisp(EMITTER emit,void *data,int len)    { printf("unsupported\n"); }
int jit_massoc(EMITTER emit,void *data,int len)  { printf("unsupported\n"); }


COMPILER compilers[] = {jit_asm,jit_edict,jit_xml,jit_json,jit_yaml,jit_swagger,jit_lisp,jit_massoc};


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

LTV *compile(COMPILER compiler,void *data,int len)
{
    char *buf=NULL;
    size_t flen=0;
    FILE *stream=open_memstream(&buf,&flen);

    int emit(VM_CMD *cmd) {
        unsigned unsigned_val;
        fwrite(&cmd->op,1,1,stream);
        if (cmd->op<VMOP_EXTENDED) {
            switch (cmd->len) {
                case -1:
                    cmd->len=strlen(cmd->data); // rewrite len and...
                    // ...fall thru!
                default:
                    unsigned_val=htonl(cmd->len);
                    fwrite(&unsigned_val,sizeof(unsigned_val),1,stream);
                    unsigned_val=htonl(cmd->flags);
                    fwrite(&unsigned_val,sizeof(unsigned_val),1,stream);
                    fwrite(cmd->data,1,cmd->len,stream);
                    break;
            }
        }
    }

    compiler(emit,data,len);
    fclose(stream);
    return LTV_init(NEW(LTV),buf,flen,LT_OWN|LT_BIN);
}

LTV *compile_ltv(COMPILER compiler,LTV *ltv)
{
    if (ltv->flags&LT_CVAR)
        return ltv; // FFI
    LTV *bc=compile(compiler,ltv->data,ltv->len);
    LTV_release(ltv);
    return bc;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
