
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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h> // basename

#include "cll.h"

#define _GNU_SOURCE
#define _C99
#include <stdlib.h>
#include "util.h"
#include "edict.h"

#include "trace.h" // lttng

extern int dwarf2edict(char *import,char *export);

int debug_dump=0;
int prompt=1;

struct REF;
struct TOK;
struct CONTEXT;

//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

CLL ref_repo;
int ref_count=0;

typedef enum {
    REF_NONE  = 0,
    REF_MATCH = 1<<0
} REF_FLAGS;

typedef struct REF {
    CLL lnk;
    LTI *lti;
    CLL ltvs; // hold ltv in list for refcount, or cvar's descended type
    LTVR *ltvr;
    REF_FLAGS flags;
} REF;

REF *REF_new(LTI *lti);
void REF_free(REF *ref);

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

CLL tok_repo;
int tok_count=0;

typedef enum {
    TOK_NONE     =0,

    // actionables
    TOK_FILE     =1<<0x00,
    TOK_EXPR     =1<<0x01,
    TOK_ATOM     =1<<0x02,
    TOK_TYPES    =TOK_FILE | TOK_EXPR | TOK_ATOM,

    // quarks (make up atoms)
    TOK_OPS      =1<<0x03,
    TOK_VAL      =1<<0x04,
    TOK_REF      =1<<0x05,
    TOK_ELL      =1<<0x06,
    TOK_QUARKS   =TOK_OPS | TOK_VAL | TOK_REF | TOK_ELL,

    // expr modifiers
    TOK_WS       =1<<0x07,
    TOK_REDUCE   =1<<0x0c,
    TOK_FLATTEN  =1<<0x0d,
    TOK_EXPRS    =TOK_WS | TOK_REDUCE | TOK_FLATTEN,

    // repl helpers
    TOK_REV      =1<<0x10,
    TOK_WC       =1<<0x11,
    TOK_RERUN    =1<<0x12,
    TOK_REPL     =TOK_REV | TOK_WC | TOK_RERUN,
} TOK_FLAGS;

typedef struct TOK {
    CLL lnk;
    CLL ltvs;
    REF *ref;
    CLL subtoks;
    TOK_FLAGS flags;
} TOK;

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv);
void TOK_free(TOK *tok);

void show_tok(char *pre,TOK *tok,char *post);
void show_toks(char *pre,CLL *toks,char *post);

//////////////////////////////////////////////////
// REPL Context
//////////////////////////////////////////////////

typedef struct CONTEXT {
    CLL lnk;
    CLL dict;  // cll of ltvr
    CLL anons; // cll of ltvr
    CLL toks;  // cll of tok
} CONTEXT;

CONTEXT *CONTEXT_new(TOK *tok);
void CONTEXT_free(CONTEXT *context);
void CONTEXT_release(CLL *lnk);

//////////////////////////////////////////////////
// REPL Refs
//////////////////////////////////////////////////

REF *refpush(REF *ref) { return (REF *) CLL_put(&ref_repo,&ref->lnk,HEAD); }
REF *refpop()          { return (REF *) CLL_get(&ref_repo,POP,HEAD); }

REF *REF_new(LTI *lti)
{
    REF *ref=NULL;
    if ((ref=refpop()) || ((ref=NEW(REF)) && CLL_init(&ref->lnk)))
    {
        CLL_init(&ref->ltvs);
        ref->lti=lti;
        ref->ltvr=NULL;
        ref->flags=REF_NONE;
        ref_count++;
    }
    return ref;
}

void REF_free(REF *ref)
{
    if (!ref) return;
    CLL_cut(&ref->lnk); // take it out of any list it's in
    CLL_release(&ref->ltvs,LTVR_release);
    ref->lti=NULL;
    refpush(ref);
    ref_count--;
}

//////////////////////////////////////////////////
// REPL Tokens
//////////////////////////////////////////////////

TOK *tokpush(CLL *cll,TOK *tok) { return (TOK *) CLL_put(cll,&tok->lnk,HEAD); }
TOK *tokpop(CLL *cll)           { return (TOK *) CLL_get(cll,POP,HEAD); }

TOK *TOK_new(TOK_FLAGS flags,LTV *ltv)
{
    TOK *tok=NULL;
    if (ltv && (tok=tokpop(&tok_repo)) || ((tok=NEW(TOK)) && CLL_init(&tok->lnk)))
    {
        CLL_init(&tok->ltvs);
        CLL_init(&tok->subtoks);
        tok->ref=NULL;
        tok->flags=flags;
        LTV_enq(&tok->ltvs,ltv,HEAD);
        tok_count++;
    }
    return tok;
}

void *TOK_freeref(TOK *tok)
{
    REF_free(tok->ref);
    return tok->ref=NULL;
}

void TOK_freerefs(TOK *tok)
{
    if (!tok) return;
    void *op(CLL *lnk) { return TOK_freeref((TOK *) lnk); }
    CLL_map(&tok->subtoks,FWD,op); // see descend!
}

void TOK_free(TOK *tok)
{
    if (!tok) return;
    CLL_cut(&tok->lnk); // take it out of any list it's in
    TOK *subtok;
    while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
    if (tok->ref) REF_free(tok->ref);
    CLL_release(&tok->ltvs,LTVR_release);
    tokpush(&tok_repo,tok);
    tok_count--;
}

void show_tok_flags(FILE *ofile,TOK *tok) {
    if (tok->flags&TOK_WS)      fprintf(ofile,"WS " );
    if (tok->flags&TOK_REDUCE)  fprintf(ofile,"REDUCE ");
    if (tok->flags&TOK_FLATTEN) fprintf(ofile,"FLATTEN ");
    if (tok->flags&TOK_REV)     fprintf(ofile,"REV ");
    if (tok->flags&TOK_FILE)    fprintf(ofile,"FILE ");
    if (tok->flags&TOK_EXPR)    fprintf(ofile,"EXPR ");
    if (tok->flags&TOK_ATOM)    fprintf(ofile,"ATOM ");
    if (tok->flags&TOK_OPS)     fprintf(ofile,"OPS ");
    if (tok->flags&TOK_VAL)     fprintf(ofile,"VAL ");
    if (tok->flags&TOK_REF)     fprintf(ofile,"REF ");
    if (tok->flags&TOK_ELL)     fprintf(ofile,"ELL ");
    if (tok->flags==TOK_NONE)   fprintf(ofile,"_ ");
}

void show_tok(char *pre,TOK *tok,char *post) {
    if (pre) printf("%s",pre);
    show_tok_flags(stdout,tok);
    print_ltvs("",&tok->ltvs,"",1);
    show_toks("(",&tok->subtoks,")");
    if (post) printf("%s",post);
    fflush(stdout);
}

void show_toks(char *pre,CLL *toks,char *post)
{
    void *op(CLL *lnk) { show_tok(NULL,(TOK *) lnk,NULL); return NULL; }
    if (toks) {
        if (pre) printf("%s",pre);
        CLL_map(toks,FWD,op);
        if (post) printf("%s",post);
    }
}

//////////////////////////////////////////////////
// REPL Context
//////////////////////////////////////////////////

CONTEXT *CONTEXT_new(TOK *tok)
{
    CONTEXT *context=NULL;
    if (tok && (context=NEW(CONTEXT))) {
        CLL_init(&context->dict); CLL_init(&context->anons); CLL_init(&context->toks);
        CLL_put(&context->toks,&tok->lnk,HEAD);
    }
    return context;
}

void CONTEXT_free(CONTEXT *context)
{
    if (!context) return;
    CLL_cut(&context->lnk); // remove from any list it's in
    void tok_free(CLL *lnk) { TOK_free((TOK *) lnk); }
    CLL_release(&context->anons,LTVR_release);
    CLL_release(&context->toks,tok_free);
    DELETE(context);
}

void CONTEXT_release(CLL *lnk) { CONTEXT_free((CONTEXT *) lnk); }


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////



int edict_graph(EDICT *edict) {
    int status=0;
    FILE *dumpfile;

    void descend_toks(CLL *toks,char *label) {
        void *op(CLL *lnk) {
            TOK *tok=(TOK *) lnk;
            fprintf(dumpfile,"\"%x\" [label=\"\" shape=box label=\"",tok);
            show_tok_flags(dumpfile,tok);
            fprintf(dumpfile,"\"]\n");
            fprintf(dumpfile,"\"%x\" -> \"%x\" [color=red]\n",tok,lnk->lnk[0]);
            //fprintf(dumpfile,"\"%x\" -> \"%x\"\n",tok,&tok->ltvs);
            fprintf(dumpfile,"\"%2$x\" [label=\"ltvs\"]\n\"%1$x\" -> \"%2$x\"\n",tok,&tok->ltvs);
            ltvs2dot(dumpfile,&tok->ltvs,0);
            if (CLL_HEAD(&tok->subtoks)) {
                fprintf(dumpfile,"\"%x\" -> \"%x\"\n",tok,&tok->subtoks);
                descend_toks(&tok->subtoks,"Subtoks");
            }
        }

        fprintf(dumpfile,"\"%x\" [label=\"%s\"]\n",toks,label);
        fprintf(dumpfile,"\"%x\" -> \"%x\" [color=red]\n",toks,toks->lnk[0]);
        CLL_map(toks,FWD,op);
    }

    void show_context(CONTEXT *context) {
        int halt=0;
        fprintf(dumpfile,"\"Context %x\"\n",context);
        fprintf(dumpfile,"\"A%2$x\" [label=\"Anons\"]\n\"Context %1$x\" -> \"A%2$x\" -> \"%2$x\"\n",context,&context->anons);
        ltvs2dot(dumpfile,&context->anons,0);
        fprintf(dumpfile,"\"D%2$x\" [label=\"Dict\"]\n\"Context %1$x\" -> \"D%2$x\" -> \"%2$x\"\n",context,&context->dict);
        ltvs2dot(dumpfile,&context->dict,0);
        fprintf(dumpfile,"\"Context %x\" -> \"%x\"\n",context,&context->toks);
        descend_toks(&context->toks,"Toks");
    }

    void show_contexts(char *pre,CLL *contexts,char *post)
    {
        void *op(CLL *lnk) { show_context((CONTEXT *) lnk); return NULL; }
        if (pre) fprintf(dumpfile,"%s",pre);
        CLL_map(contexts,FWD,op);
        if (post) fprintf(dumpfile,"%s",post);
    }

    if (!edict) goto done;

    dumpfile=fopen("/tmp/jj.dot","w");
    fprintf(dumpfile,"digraph iftree\n{\ngraph [/*ratio=compress, concentrate=true*/] node [shape=record] edge []\n");

    fprintf(dumpfile,"Gmymalloc [label=\"Gmymalloc %d\"]\n",Gmymalloc);
    fprintf(dumpfile,"ltv_count [label=\"ltv_count %d\"]\n",ltv_count);
    fprintf(dumpfile,"ltvr_count [label=\"ltvr_count %d\"]\n",ltvr_count);
    fprintf(dumpfile,"lti_count [label=\"lti_count %d\"]\n",lti_count);

    show_contexts("",&edict->contexts,"\n");

    fprintf(dumpfile,"}\n");
    fclose(dumpfile);

done:
    return status;
}


#define SEP "."
#define OPS "$@/!&|=<>(){}#"
#define ATOM_END (WHITESPACE OPS "<>(){}\'\"")

int parse(TOK *tok)
{
    int status=0;
    char *data=NULL,*tdata=NULL;
    int len=0,tlen=0;
    LTV *tokval=NULL;

    int advance(unsigned x) { x=MIN(x,len); data+=x; len-=x; return x; }

    TOK *append(TOK *tok,int type,char *data,int len,int adv) {
        TOK *subtok=TOK_new(type,LTV_new(data,len,LT_DUP));
        if (!subtok) return NULL;
        advance(adv);
        return (TOK *) CLL_splice(&tok->subtoks,&subtok->lnk,TAIL);
    }

    STRY(!tok,"testing for null tok");
    //TRYCATCH(!(tokval=LTV_deq(&tok->ltvs,HEAD)),0,done,"testing for tok ltvr value");
    STRY(!(tokval=LTV_deq(&tok->ltvs,HEAD)),"testing for tok ltvr value");

    STRY(tokval->flags&LT_NSTR,"testing for non-string tok ltvr value");
    STRY(!tokval->data,"testing for null tok ltvr data");

    data=tokval->data;
    len=tokval->len;

    if (tok->flags&TOK_EXPR) while (len) {
        switch (*data) {
            case '\\': advance(1);
            case ' ':
            case '\t':
            case '\n': tlen=series(data,len,WHITESPACE,NULL,NULL);      STRY(!append(tok,TOK_EXPR|TOK_WS,     data  ,tlen  ,tlen),"appending ws");       break;
            case '[':  tlen=series(data,len,NULL,NULL,"[]");            STRY(!append(tok,TOK_EXPR|TOK_VAL,    data+1,tlen-2,tlen),"appending lit");      break;
            case '\'': tlen=series(data,len,NULL,NULL,"\'\'");          STRY(!append(tok,TOK_EXPR|TOK_REDUCE, data+1,tlen-2,tlen),"appending reduce");   break;
            case '\"': tlen=series(data,len,NULL,NULL,"\"\"");          STRY(!append(tok,TOK_EXPR|TOK_FLATTEN,data+1,tlen-2,tlen),"appending flatten");  break;
            case '<': case '>': case '(': case ')': case '{': case '}': STRY(!append(tok,TOK_ATOM,            data  ,     1,   1),"appending %c",*data); break; // special, non-ganging op, no balance!
            default:   tlen=series(data,len,SEP OPS,ATOM_END,"[]");     STRY(!append(tok,TOK_ATOM,            data  ,tlen  ,tlen),"appending atom");     break;
        }
    }
    else if (tok->flags&TOK_ATOM) while (len) {
        TOK *ops=NULL,*ref=tok,*val=NULL;

        if (tlen=series(data,len,OPS,NULL,NULL)) // ops
            STRY(!(ops=append(tok,TOK_OPS,data,tlen,tlen)),"appending ops");

        while (len) {
            if ((tlen=series(data,len,NULL,SEP "[",NULL))) {
                int reverse=advance(data[0]=='-')?TOK_REV:0;
                int wildcard=series(data,len,NULL,"*?",NULL)<tlen?TOK_WC:0;
                if (reverse) tlen-=1;
                STRY(!(ref=append(tok,TOK_REF|reverse|wildcard,data,tlen,tlen)),"appending ref");
            }
            while ((tlen=series(data,len,NULL,NULL,"[]")))
                STRY(!(val=append(ref,TOK_VAL,data+1,tlen-2,tlen)),"appending val");
            if ((tlen=series(data,len,SEP,NULL,NULL))) {
                if (tlen==3)
                    STRY(!(ref=append(tok,TOK_ELL,NULL,0,0)),"appending ellipsis");
                advance(tlen);
            }
        }
    }

    done:
    return status;
}

#define LTV_NIL  LTV_new(NULL,0,LT_NIL)
#define LTV_NULL LTV_new(NULL,0,LT_NULL)
// FIXME: name ideas: "Harness", "Munkeywrench"

#define CONDITIONAL_BAIL 1

enum {
    DEBUG_FILE      = 1<<0,
    DEBUG_ATOM      = 1<<1,
    DEBUG_EXPR      = 1<<2,
    DEBUG_BAIL      = 1<<3,
    DEBUG_PREEVAL   = 1<<4,
    DEBUG_POSTEVAL  = 1<<5,
    DEBUG_ERR       = 1<<6
};

TOK *edict_expr(char *expr) {
    return TOK_new(TOK_EXPR,LTV_new(expr,-1,LT_DUP));
}


void *edict_lookup(TOK *atom_tok,TOK *ref_tok,int insert,LTV *origin) {
    int status=0;
    TOK *acc_tok=ref_tok;

    void *descend(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        int status=0;
        TOK *next_tok=acc_tok; // a little finesse...

        if (*ltv) {

            // need to be able to descend from either TOS or a named node
            if ((*ltv)->flags&LT_CVAR) {
                // Iterate through cvar member hierarchy from here, overwriting ref per iteration
                //STRY(edict_cvar_resolve(),"resolve cvar member");
            }

            if (acc_tok->ref && acc_tok->ref->lti) {
                *lti=acc_tok->ref->lti;
            } else {
                LTV *ref_ltv=NULL;
                STRY(!(ref_ltv=LTV_peek(&acc_tok->ltvs,HEAD)),"getting token name");
                int inserted=insert && !((*ltv)->flags&LT_RO) && (!(*ltvr) || !((*ltvr)->flags&LT_RO)); // directive on way in, status on way out
                if ((status=!((*lti)=RBR_find(&(*ltv)->sub.ltis,ref_ltv->data,ref_ltv->len,&inserted))))
                    goto done;
                if (acc_tok->ref)
                    acc_tok->ref->lti=*lti;
                else
                    STRY(!(acc_tok->ref=REF_new(*lti)),"allocating ref");
            }
        }
        else if (*lti) {
            next_tok=(TOK *) CLL_next(&atom_tok->subtoks,&acc_tok->lnk,FWD); // we're iterating through atom_tok's subtok list
            if (acc_tok->ref && acc_tok->ref->ltvr) { // ltv always accompanies ltvr so no need to check
                *ltvr=acc_tok->ref->ltvr;
                *ltv=(*ltvr)->ltv;
            } else {
                int reverse=acc_tok->flags&TOK_REV;
                LTV *val_ltv=NULL;
                TOK *subtok=(TOK *) CLL_get(&acc_tok->subtoks,KEEP,HEAD);
                if (subtok) {
                    STRY(!(val_ltv=LTV_peek(&subtok->ltvs,HEAD)),"getting ltvr w/val from token");
                    reverse |= subtok->flags&TOK_REV;
                }
                char *match=val_ltv?val_ltv->data:NULL;
                int matchlen=val_ltv?-1:0;
                if (match)
                    acc_tok->ref->flags|=REF_MATCH;
                (*ltv)=LTV_get(&(*lti)->ltvs,KEEP,reverse,match,matchlen,&(*ltvr)); // lookup

                // check if add is required
                if (!(*ltv) && insert) {
                    if (next_tok && !val_ltv) // insert a null ltv to build hierarchical ref
                        val_ltv=LTV_NULL;
                    if (val_ltv)
                        (*ltv)=LTV_put(&(*lti)->ltvs,val_ltv,reverse,&(*ltvr));
                }

                if (*ltvr && *ltv) {
                    LTV_enq(&acc_tok->ref->ltvs,(*ltv),HEAD); // ensure it's referenced
                    acc_tok->ref->ltvr=*ltvr;
                }
            }
        }
        else if (*ltvr)
            return NULL; // early exit in this case.

        done:
        if (status)
            *flags=LT_TRAVERSE_HALT;

        if (!next_tok) // only advanced by *lti path
            return acc_tok;
        // else
        acc_tok=next_tok;
        return NULL;
    }

    TOK_freerefs(ref_tok);
    TOK *rtok=(TOK *) listree_traverse(origin,descend,NULL);
    return status?NULL:rtok;
}



int edict_eval(EDICT *edict)
{
    int status=0;

    int eval_context(CONTEXT *context) { /////////////////////////////////////////
        int status=0;
        TOK *tok;

        int eval_tok(TOK *tok) { /////////////////////////////////////////
            static int debug=DEBUG_BAIL|DEBUG_ERR;

            int inject(char *edict_code) { /////////////////////////////////////////
                int status=0;
                TOK *expr=NULL;
                STRY(!(expr=TOK_new(TOK_EXPR,LTV_new(edict_code,-1,LT_NONE))),"allocating injected expr tok");
                STRY(eval_tok(expr),"evaluating injected expression");
                done:
                return status;
            }

            int eval_atom(TOK *atom_tok) { /////////////////////////////////////////
                void *resolve_ref(TOK *ref_tok,int insert) { /////////////////////////////////////////
                    void *resolve_stackframe(CLL *lnk) { /////////////////////////////////////////
                        if (!lnk) return NULL;
                        LTVR *ltvr=(LTVR *) lnk;
                        return edict_lookup(atom_tok,ref_tok,insert,ltvr->ltv);
                    }

                    //TOK_freerefs(ref_tok);
                    return CLL_map(&context->dict,FWD,resolve_stackframe);
                }

                void *iterate_ref(TOK *ref_tok) { /////////////////////////////////////////
                    TOK *rtok=(TOK *) CLL_TAIL(&atom_tok->subtoks);
                    if (!rtok || !rtok->ref)
                        return resolve_ref(ref_tok,0);
                    else { // long version A) while no next-item, walk subtoks in reverse. B) re-resolve back to the end using (a cut-down version of) resolve_ref()
                        // shortcut: iterate over just the tail of the stack, i.e. assume ref is fully resolved with no wildcards above (and no list of specific vals)
                        // shortcut2: if "name" is a wildcard name, iterate to the next matching name
                        int reverse=rtok->flags&TOK_REV;
                        REF *ref=rtok->ref;
                        LTVR *ltvr=NULL;
                        if (ref->lti && ref_tok->flags&TOK_WC) {
                            LTV *ref_ltv=NULL;
                            LTI *lti=NULL;
                            ref_ltv=LTV_peek(&ref_tok->ltvs,HEAD);
                            for (lti=LTI_next(ref->lti); lti && fnmatch_len(ref_ltv->data,ref_ltv->len,lti->name,-1); lti=LTI_next(lti)) {}
                            if (lti) {
                                TOK_freeref(ref_tok);
                                ref=ref_tok->ref=REF_new(lti);
                            }
                        }
                        if (!ref->lti || (ltvr=(LTVR *) CLL_next(&ref->lti->ltvs,ref->ltvr?&ref->ltvr->lnk:NULL,reverse))) {
                            LTV_release(LTV_deq(&ref->ltvs,HEAD));
                            LTV_enq(&ref->ltvs,ltvr->ltv,HEAD); // ensure that ltv is referenced by at least one thing so it won't disappear
                            ref->ltvr=ltvr;
                            return rtok;
                        }
                    }
                }

                int lookup(TOK *expr,LTV **ltv) { /////////////////////////////////////////
                    int status=0;

                    STRY(!((*ltv)=LTV_peek(&context->anons,HEAD)),"popping lookup result");
                    done:
                    return status;
                }

                int import()  { /////////////////////////////////////////
                    int status=0;
                    LTV *ltv_ifilename=NULL;
                    STRY(!(ltv_ifilename=LTV_deq(&context->anons,HEAD)),"popping import filename");
                    char *ifilename=bufdup(ltv_ifilename->data,ltv_ifilename->len);
                    TOK *file_tok=TOK_new(TOK_FILE,LTV_new((void *) fopen(ifilename,"r"),sizeof(FILE *),LT_IMM));
                    STRY(!CLL_put(&context->toks,&file_tok->lnk,HEAD),"pushing file");
                    myfree(ifilename,strlen(ifilename)+1);
                    LTV_release(ltv_ifilename);
                    done:
                    return status;
                }

                int d2e() { /////////////////////////////////////////
                    int status=0;
                    LTV *ltv_ifilename=NULL,*ltv_ofilename=NULL;
                    STRY(!(ltv_ifilename=LTV_deq(&context->anons,HEAD)),"popping dwarf import filename");
                    STRY(!(ltv_ofilename=LTV_peek(&context->anons,HEAD)),"peeking edict export filename");
                    char *ifilename=bufdup(ltv_ifilename->data,ltv_ifilename->len);
                    char *ofilename=bufdup(ltv_ofilename->data,ltv_ofilename->len);
                    dwarf2edict(ifilename,ofilename);
                    myfree(ifilename,strlen(ifilename)+1);
                    myfree(ofilename,strlen(ofilename)+1);
                    LTV_release(ltv_ifilename);
                    done:
                    return status;
                }

                int cvar() { /////////////////////////////////////////
                    int status=0;
                    LTV *type, *cvar;
                    STRY(!(type=LTV_deq(&context->anons,HEAD)),"popping type");
                    int size = 100; // TODO: figure this out
                    STRY(!(cvar=LTV_new((void *) mymalloc(size),size,LT_CVAR | LT_OWN | LT_BIN | LT_LIST)),"allocating cvar ltv"); // very special node!
                    STRY(!LTV_enq(&(cvar->sub.ltvs),type,HEAD),"pushing type into cvar");
                    STRY(!LTV_enq(&context->anons,cvar,HEAD),"pushing cvar");
                    STRY(inject("#"),"injecting #");
                    done:
                    return status;
                }

                int resolve_ops(TOK *tok,char *ops,int opslen) { /////////////////////////////////////////
                    int status=0;

                    int deref(int getnext) { /////////////////////////////////////////
                        int status=0;
                        LTV *ltv=NULL;
                        TOK *rtok=NULL;
                        STRY(!tok,"validating tok");
                        rtok=(TOK *) (getnext?iterate_ref(tok):resolve_ref(tok,0));
                        if (rtok && rtok->ref && (ltv=LTV_peek(&rtok->ref->ltvs,HEAD)))
                            STRY(!LTV_enq(&context->anons,ltv,HEAD),"pushing ltv to anons");
                        else
                            status=CONDITIONAL_BAIL; //  ltv=LTV_NIL;
                        done:
                        return status;
                    }

                    int hash() { /////////////////////////////////////////
                        int status=0;
                        TOK *rtok=NULL;
                        STRY(!(rtok=(TOK *) resolve_ref(tok,0)),"resolving tok for '#'");
                        STRY(!(rtok->ref && rtok->ref->lti),"validating rtok's ref, ref->lti");
                        CLL *ltvs=(rtok->ref->flags&REF_MATCH)?&rtok->ref->ltvs:&rtok->ref->lti->ltvs;
                        graph_ltvs(ltvs,0);
                        print_ltvs(CODE_BLUE,ltvs,CODE_RESET "\n",2);
                        done:
                        return status;
                    }


                    atom_tok->flags&=~TOK_RERUN; // discard tok by default

                    for (int i=0;i<opslen;i++) {
                        switch (ops[i]) {
                            case '.': { // deref from TOS (questionabie utilty right now)
                                LTV *ltv=NULL;
                                STRY(!(ltv=LTV_peek(&context->anons,HEAD)),"peeking anon");
                                /// ???
                                break;
                            }
                            case '$': {
                                STRY(deref(0),"resolving dereference");
                                break;
                            }
                            case '@': { // resolve refs needs to not worry about last ltv, just the lti is important.
                                STRY(!tok,"validating tok");
                                LTV *ltv=NULL;
                                TOK *rtok=NULL;
                                STRY(!(ltv=LTV_deq(&context->anons,HEAD)),"popping anon");
                                STRY(!(rtok=(TOK *) resolve_ref(tok,1)),"looking up reference for '@'");
                                STRY(!(rtok->ref && rtok->ref->lti),"validating rtok's ref, ref->lti");
                                STRY(!LTV_enq(&rtok->ref->lti->ltvs,ltv,rtok->flags&TOK_REV),"adding anon to lti");
                                break;
                            }
                            case '/': {
                                TOK *rtok=NULL;
                                if (tok) {
                                    TRYCATCH(!(rtok=(TOK *)  resolve_ref(tok,0)),0,nothing_to_release,"looking up reference for '/'");
                                    TRYCATCH(!(rtok->ref) || !(rtok->ref->ltvr),0,nothing_to_release,"getting rtok ref ltvr");
                                    LTVR_release(&rtok->ref->ltvr->lnk);
                                    TOK_freeref(rtok);
                                }
                                else {
                                    LTV_release(LTV_deq(&context->anons,HEAD));
                                }
                                nothing_to_release:
                                break;
                            }
                            case '<': // push anon onto the scope stack
                                STRY(!LTV_enq(&context->dict,LTV_deq(&context->anons,HEAD),HEAD),"pushing anon scope");
                                break;
                            case '>': // pop scope stack back onto anon
                                STRY(!LTV_enq(&context->anons,LTV_deq(&context->dict,HEAD),HEAD),"popping anon scope");
                                break;
                            case '(': // push anon onto the scope stack (for later exec?)
                                STRY(!LTV_enq(&context->dict,LTV_deq(&context->anons,HEAD),HEAD),"pushing anon scope");
                                break;
                            case ')': { // exec top of scope stack
                                STRY(!tok,"validating tok");
                                TOK *expr=TOK_new(TOK_EXPR,LTV_deq(&context->dict,HEAD));
                                STRY(!CLL_put(&context->toks,&expr->lnk,HEAD),"pushing exec expr lambda");
                                break;
                            }
                            case '{': case '}': break; // placeholder
                            case '&': {
                                LTV *ltv=NULL;
                                STRY(!(ltv=LTV_peek(&context->anons,HEAD)),"peeking anon");
                                TRYCATCH((ltv->flags&LT_NIL)!=0,CONDITIONAL_BAIL,done,"testing for nil");
                                break;
                            }
                            case '|': {
                                LTV *ltv=NULL;
                                STRY(!(ltv=LTV_peek(&context->anons,HEAD)),"peeking anon");
                                TRYCATCH((ltv->flags&LT_NIL)==0,CONDITIONAL_BAIL,done,"testing for non-nil");
                                break;
                            }
                            case '=': // compare either TOS/NOS, or TOS/name
                                break;
                            case '!': { // limit wildcard dereferences to exec-with-name!!!
                                LTV *lambda_ltv=NULL;

                                STRY(!(lambda_ltv=LTV_deq(&context->anons,HEAD)),"popping lambda"); // pop lambda

                                if (!tok) {
                                    TOK *lambda_tok=TOK_new(TOK_EXPR,lambda_ltv);
                                    STRY(!CLL_put(&context->toks,&lambda_tok->lnk,HEAD),"pushing lambda");
                                }
                                else if (!deref(1)) { //something going on here, the last iteration fails to pop the lambda!!!!!
                                    atom_tok->flags|=TOK_RERUN;

                                    TOK *lambda_tok=TOK_new(TOK_EXPR | TOK_VAL,lambda_ltv);
                                    STRY(!CLL_put(&context->toks,&lambda_tok->lnk,HEAD),"pushing lambda"); // to enq anon for later...

                                    lambda_tok=TOK_new(TOK_EXPR,lambda_ltv);
                                    STRY(!CLL_put(&context->toks,&lambda_tok->lnk,HEAD),"pushing lambda"); // to exec now
                                }
                                else
                                    LTV_release(lambda_ltv);

                                break;
                            }
                            case '#': {
                                if (tok) {
                                    LTV *ltv=NULL;
                                    if ((ltv=LTV_peek(&tok->ltvs,HEAD))) {
                                        if      (!strnncmp(ltv->data,ltv->len,"import",-1)) STRY(import(),"importing edict");
                                        else if (!strnncmp(ltv->data,ltv->len,"d2e",-1))    STRY(d2e(),"converting dwarf to edict");
                                        else if (!strnncmp(ltv->data,ltv->len,"cvar",-1))   STRY(cvar(),"creating cvar");
                                        else                                                STRY(hash(),"dumping named item");
                                    }
                                } else {
                                    edict_graph(edict);
                                    print_ltvs(CODE_BLUE,&context->anons,CODE_RESET "\n",0);
                                }
                                break;
                            }
                            default:
                                printf("skipping unrecognized OP %c (%d)",ops[i],ops[i]);
                                break;
                        }
                    }

                    // FIXME: Clean up!!!

                    done:
                    return status;
                }

                int status=0;

                if (!CLL_HEAD(&atom_tok->subtoks))
                    STRY(parse(atom_tok),"parsing");

                TOK *tos=(TOK *) CLL_HEAD(&atom_tok->subtoks);

                if (tos->flags&TOK_OPS) {
                    LTV *ltv=NULL; // optok's data
                    STRY(!(ltv=LTV_peek(&tos->ltvs,HEAD)),"getting optok data");
                    TRYCATCH(resolve_ops((TOK *) CLL_next(&atom_tok->subtoks,&tos->lnk,FWD),ltv->data,ltv->len),status,done,"resolving op/ref"); // pass status thru
                }
                else if (tos->flags&TOK_REF)
                    STRY(resolve_ops(tos,"$",1),"resolving implied ref");
                else if (tos->flags&TOK_VAL)
                    STRY(!LTV_enq(&context->anons,LTV_deq(&tos->ltvs,HEAD),HEAD),"pushing anon lit");

                done:
                if (!status && !(atom_tok->flags&TOK_RERUN))
                    TOK_free(atom_tok);

                return status;
            }

            int eval_expr(TOK *tok) { /////////////////////////////////////////
                int status=0;

                if (debug&DEBUG_PREEVAL)
                    edict_graph(edict);

                if (CLL_EMPTY(&tok->subtoks)) {
                    if (tok->flags&TOK_VAL)
                        STRY(!LTV_enq(&context->anons,LTV_deq(&tok->ltvs,HEAD),HEAD),"pushing expr lit");
                    else if (tok->flags&TOK_WS)
                        ;
                    else if (tok->flags&TOK_REDUCE)
                        ;
                    else if (tok->flags&TOK_FLATTEN)
                        ;
                    else
                        STRY(parse(tok),"parsing expr");
                }

                if (!CLL_EMPTY(&tok->subtoks))
                    STRY(eval_tok((TOK *) CLL_HEAD(&tok->subtoks)),"evaluating expr subtoks");

                done:
                if (debug&DEBUG_POSTEVAL)
                    edict_graph(edict);

                if (status) {
                    if (status==CONDITIONAL_BAIL) {
                        if (debug&DEBUG_BAIL)
                            show_tok("Bailing on expression: [",tok,"]\n");
                    } else if (debug&DEBUG_ERR) {
                        show_tok("Error evaluating expression: [",tok,"]\n");
                    }

                    TOK *subtok;
                    while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);
                    status=0;
                }

                if (CLL_EMPTY(&tok->subtoks))
                    TOK_free(tok); // evaluation done; Could add some kind of sentinel atom in here to let the expression repopulate itself and continue
                return status;
            }

            int eval_file(TOK *tok) { /////////////////////////////////////////
                int status=0;
                char *line;
                int len;
                TOK *expr=NULL;
                LTV *tok_data;

                STRY(!tok,"validating file tok");
                STRY(!(tok_data=LTV_peek(&tok->ltvs,HEAD)),"validating file");
                if (stdin==(FILE *) tok_data->data) { printf(CODE_BLUE "j2> " CODE_RESET); fflush(stdout); }
                if (debug&DEBUG_FILE) edict_graph(edict);
                TRYCATCH((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,0,close_file,"reading from file");
                TRYCATCH(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),TRY_ERR,free_line,"allocating expr tok");
                TRYCATCH(!tokpush(&context->toks,expr),TRY_ERR,free_expr,"enqueing expr token");
                goto done; // success

                free_expr:  TOK_free(expr);
                free_line:  free(line);
                close_file:
                fclose((FILE *) tok_data->data);
                TOK_free(tok);

                done:
                return status;
            }

            STRY(!tok,"testing for null tok");

            switch(tok->flags&TOK_TYPES)
            {
                case TOK_FILE: STRY(eval_file(tok),"evaluating file"); break;
                case TOK_EXPR: STRY(eval_expr(tok),"evaluating expr"); break;
                case TOK_ATOM: TRYCATCH(eval_atom(tok),status,done,"evaluating atom"); break; // pass status thru
                default: TOK_free(tok); break;
            }

            done:
            return status;
        }

        STRY(!context,"testing for null context");

        if (eval_tok((TOK *) CLL_get(&context->toks,KEEP,HEAD)))
            CONTEXT_free(context);

        done:
        return status;
    }

    try_reset();
    while (!eval_context((CONTEXT *) CLL_ROT(&edict->contexts,FWD)));

 done:
    return status;
}




///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// Initialization
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

int edict_init(EDICT *edict)
{
    int status=0;
    TSTART(0,"");
    LTV *root=LTV_new("ROOT",TRY_ERR,0);
    LT_init();
    STRY(!edict,"validating arg edict");
    BZERO(*edict);
    STRY(!LTV_enq(CLL_init(&edict->dict),root,HEAD),"pushing edict->dict root");
    STRY(!CLL_init(&tok_repo),"initializing tok_repo");
    STRY(!CLL_init(&ref_repo),"initializing ref_repo");
    STRY(!CLL_init(&edict->contexts),"initializing context list");
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,LTV_new((void *) stdin,sizeof(FILE *),LT_IMM)));
    STRY(!LTV_enq(&context->dict,root,HEAD),"pushing context->dict root");
    STRY(!CLL_put(&edict->contexts,&context->lnk,HEAD),"pushing edict's initial context");
    STRY(edict_eval(edict),"evaluating edict contexts");

 done:
    TFINISH(0,"");
    return status;
}

int edict_destroy(EDICT *edict)
{
    int status=0;
    STRY(!edict,"validating arg edict");
    CLL_release(&edict->dict,LTVR_release);
    CLL_release(&edict->contexts,CONTEXT_release);
 done:
    return status;
}
