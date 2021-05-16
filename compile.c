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

#include <dlfcn.h>
#include <arpa/inet.h>

#include "util.h"
#include "compile.h"
#include "vm.h"

#define EMIT(bc) emit(&((VM_CMD) {VMOP_ ## bc}))
#define EMIT_EXT(data,len,flags) emit(&((VM_CMD) {VMOP_EXT,(len),(flags),(data)}));

int jit_asm(EMITTER emit,void *data,int len)
{
    VM_CMD *cmd=(VM_CMD *) data;
    for (int i=0;i<len;i++)
        emit(cmd+i);
    EMIT(YIELD);
}

#define EDICT_OPS "@/!&|^"
#define EDICT_MONO_OPS "()<>"

int jit_edict(EMITTER emit,void *data,int len)
{
    int status=0;
    char *tdata=(char *) data;
    int tlen=0;

    int advance(int adv)  { adv=MIN(adv,len); tdata+=adv; len-=adv; return adv; };
    int skip_whitespace() { advance(series(tdata,len,WHITESPACE,NULL,NULL)); return true; };
    int compile_term() {
        if ((tlen=series(tdata,len,NULL,NULL,"[]"))) {
            EMIT_EXT(tdata+1,tlen-2,LT_DUP); EMIT(PUSHEXT);
            advance(tlen);
        }
        else if ((tlen=series(tdata,len,EDICT_MONO_OPS,NULL,NULL))) {
            for (int i=0;i<tlen;i++)
                switch (tdata[i]) {
                    case '<': EMIT(CTX_PUSH); break;
                    case '>': EMIT(CTX_POP);  break;
                    case '(': EMIT(FUN_PUSH); break;
                    case ')': EMIT(FUN_EVAL); break;
                    default: break;
                }
            advance(tlen);
        } else { // no block, look for an atom
            char *ops_data=tdata;
            int ops_len=series(tdata,len,EDICT_OPS,NULL,NULL);
            advance(ops_len);
            int ref_len=0;
            while ((tlen=series(tdata+ref_len,len-ref_len,NULL,NULL,"''")) || // quoted ref component
                   (tlen=series(tdata+ref_len,len-ref_len,NULL,WHITESPACE EDICT_OPS EDICT_MONO_OPS "'","[]"))) // unquoted ref
                ref_len+=tlen;

            tlen=ops_len+ref_len;

            if (!ops_len && ref_len==2 && tdata[0]=='$') { // possible keyword
                switch (tdata[1]) {
                    case 's': EMIT(S2S); advance(ref_len); goto done; // dup
                    case 'd': EMIT(D2S); advance(ref_len); goto done; // TOS[dict] -> stack
                    case 'e': EMIT(E2S); advance(ref_len); goto done; // TOS[excp] -> stack
                    case 'f': EMIT(F2S); advance(ref_len); goto done; // TOS[func] -> stack
                    case 'D': EMIT(S2D); advance(ref_len); goto done; // stack -> TOS[dict]
                    case 'E': EMIT(S2E); advance(ref_len); goto done; // stack -> TOS[excp]
                    case 'F': EMIT(S2F); advance(ref_len); goto done; // stack -> TOS[func]
                    default: break;
                }
            } else if (ref_len) {
                EMIT_EXT(tdata,ref_len,LT_DUP);
                advance(ref_len);
            } else
                EMIT(RESET);

            if (ops_len && ops_data[0]=='|') // catch is a special case
                EMIT(CATCH);
            else {
                if (ref_len)
                    EMIT(REF);
                if (!ops_len)
                    EMIT(DEREF);
                for (int i=0;i<ops_len;i++) {
                    switch (ops_data[i]) {
                        case '@': EMIT(ASSIGN);  break;
                        case '/': EMIT(REMOVE);  break;
                        case '!': EMIT(EVAL);    break;
                        case '&': EMIT(THROW);   break;
                        case '^': EMIT(PUSHEXT); break;
                    }
                }
            }
        }
    done:
        return tlen;
    }

    STRY(!tdata,"testing source code");
    while (skip_whitespace() && len && compile_term());
    EMIT(YIELD);

 done:
    return status;
}

int jit_xml(EMITTER emit, void *data, int len) { printf("jit_xml not implemented\n"); }

/*
int json_object(EMITTER emit,void *data,int len) {
    char *tdata=(char *) data;
    int tlen=0;

    int advance(int adv)  { adv=MIN(adv,len); tdata+=adv; len-=adv; return adv; };
    int whitespace() { advance(series(tdata,len,WHITESPACE,NULL,NULL)); return true; };

    whitespace();
    tlen=series(tdata, len, WHITESPACE, NULL, "{}"));
}

int json_string(EMITTER emit)
int jit_json(EMITTER emit, void *data, int len)
{
    int status=0;
    char *tdata=(char *) data;
    int tlen=0;

    int advance(int adv)  { adv=MIN(adv,len); tdata+=adv; len-=adv; return adv; };
    int whitespace() { advance(series(tdata,len,WHITESPACE,NULL,NULL)); return true; };
    int string(int validate) {
        whitespace();
        tlen = series(tdata, tlen, NULL, NULL, "\"\"");
        EMIT_EXT(tdata, tlen, LT_DUP);
    }
    int number(LTV *ltv) { whitespace(); }
    int value(LTV *ltv) { whitespace(); }
    int array(LTV *ltv) { whitespace(); }
    int object(LTV *ltv) {
        int olen = 0;
        if (whitespace() && (olen = series(tdata, tlen, NULL, NULL, "{}")))
        {
            do {

            } while (whitespace() &&tlen = series(tdata, tlen, ));
            advance()
        }
    }

    int compile_term() {
        whitespace();
        if ((tlen = series(tdata, len, NULL, NULL, "[]")))
        {
            EMIT_EXT(tdata+1,tlen-2,LT_DUP); EMIT(PUSHEXT);
            advance(tlen);
        }
        else if ((tlen=series(tdata,len,EDICT_MONO_OPS,NULL,NULL))) {
            for (int i=0;i<tlen;i++)
                switch (tdata[i]) {
                    case '<': EMIT(CTX_PUSH); break;
                    case '>': EMIT(CTX_POP);  break;
                    case '(': EMIT(FUN_PUSH); break;
                    case ')': EMIT(FUN_EVAL); break;
                    default: break;
                }
            advance(tlen);
        } else { // no block, look for an atom
            char *ops_data=tdata;
            int ops_len=series(tdata,len,EDICT_OPS,NULL,NULL);
            advance(ops_len);
            int ref_len=0;
            while ((tlen=series(tdata+ref_len,len-ref_len,NULL,NULL,"''")) || // quoted ref component
                   (tlen=series(tdata+ref_len,len-ref_len,NULL,WHITESPACE EDICT_OPS EDICT_MONO_OPS "'","[]"))) // unquoted ref
                ref_len+=tlen;

            tlen=ops_len+ref_len;

            if (!ops_len && ref_len==2 && tdata[0]=='$') { // possible keyword
                switch (tdata[1]) {
                    case 's': EMIT(S2S); advance(ref_len); goto done; // dup
                    case 'd': EMIT(D2S); advance(ref_len); goto done; // TOS[dict] -> stack
                    case 'e': EMIT(E2S); advance(ref_len); goto done; // TOS[excp] -> stack
                    case 'f': EMIT(F2S); advance(ref_len); goto done; // TOS[func] -> stack
                    case 'D': EMIT(S2D); advance(ref_len); goto done; // stack -> TOS[dict]
                    case 'E': EMIT(S2E); advance(ref_len); goto done; // stack -> TOS[excp]
                    case 'F': EMIT(S2F); advance(ref_len); goto done; // stack -> TOS[func]
                    default: break;
                }
            }

            if (ref_len) {
                EMIT_EXT(tdata,ref_len,LT_DUP);
                advance(ref_len);
            } else
                EMIT(RESET);

            if (ops_len && ops_data[0]=='|') // catch is a special case
                EMIT(CATCH);
            else {
                if (ref_len)
                    EMIT(REF);
                if (!ops_len)
                    EMIT(DEREF);
                for (int i=0;i<ops_len;i++) {
                    switch (ops_data[i]) {
                        case '@': EMIT(ASSIGN);  break;
                        case '/': EMIT(REMOVE);  break;
                        case '!': EMIT(EVAL);    break;
                        case '&': EMIT(THROW);   break;
                        case '^': EMIT(PUSHEXT); break;
                    }
                }
            }
        }
    done:
        return tlen;
    }

    STRY(!tdata,"testing source code");
    while (len && compile_term());

 done:
    return status;
 }
*/

LTV *compile(COMPILER compiler,void *data,int len)
{
    TSTART(0,"compile");
    char *buf=NULL;
    size_t flen=0;
    FILE *stream=open_memstream(&buf,&flen);

    int emit(VM_CMD *cmd) {
        unsigned unsigned_val;
        fwrite(&cmd->op,1,1,stream);
        if (cmd->op==VMOP_EXT) {
            switch (cmd->len) {
                case -1:
                    cmd->len=strlen(cmd->data); // rewrite len and...
                    // ...fall thru!
                default:
                    unsigned_val=htonl(cmd->len);
                    fwrite(&unsigned_val,sizeof(unsigned),1,stream);
                    unsigned_val=htonl(cmd->flags);
                    fwrite(&unsigned_val,sizeof(unsigned),1,stream);
                    fwrite(cmd->data,1,cmd->len,stream);
                    break;
            }
        }
    }

    if (len==-1)
        len=strlen((char *) data);
    compiler(emit,data,len);
    fclose(stream);
    TFINISH(0,"compile");
    return LTV_init(NEW(LTV),buf,flen,LT_BC|LT_OWN|LT_BIN|LT_LIST);
}

LTV *compile_ltv(COMPILER compiler,LTV *ltv)
{
    // print_ltv(stdout,CODE_RED "compile: ",ltv,CODE_RESET "\n",0);
    if (ltv->flags&(LT_CVAR|LT_BC))
        return ltv; // FFI or pre-compiled
    LTV *bc=compile(compiler,ltv->data,ltv->len);
    return bc;
}

char *opcode_name[] = { "RESET","YIELD","EXT","THROW","CATCH","PUSHEXT","EVAL","REF","DEREF","ASSIGN","REMOVE","CTX_PUSH","CTX_POP","FUN_PUSH","FUN_EVAL","FUN_POP",
                       "S2S","D2S","E2S","F2S","S2D","S2E","S2F" };

void disassemble(FILE *ofile,LTV *ltv)
{
    TSTART(0,"disassemble");
    unsigned char *data,*code=(unsigned char *) ltv->data;
    int i=0,length=0,flags=0;
    fprintf(ofile,"BYTECODE: ");
    while (i<ltv->len) {
        unsigned char opcode=code[i++];
        switch(opcode) {
            case VMOP_EXT:
                length=ntohl(*(unsigned *) (code+i)); i+=sizeof(unsigned);
                flags=ntohl(*(unsigned *)  (code+i)); i+=sizeof(unsigned);
                data=code+i;                          i+=length;
                //fprintf(ofile, "\n" CODE_BLUE);
                fprintf(ofile, CODE_BLUE);
                fstrnprint(ofile,data,length);
                fprintf(ofile, ":%x " CODE_RESET, flags);
                break;
            case VMOP_RESET:
                //fprintf(ofile,"\n");
            default:
                fprintf(ofile,"%s ",opcode_name[opcode]);
        }
    }
    fprintf(ofile,"\n");
    TFINISH(0,"disassemble");
}
