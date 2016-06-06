
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

#include "cll.h"


#define _GNU_SOURCE
#define _C99
#include <stdlib.h>
#include "util.h"
#include "edict.h"

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

typedef struct REF {
    CLL lnk;
    LTI *lti;
    CLL ltvs; // hold ltv in list for refcount
    LTVR *ltvr;
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
    TOK_NOTE     =1<<0x08,
    TOK_REDUCE   =1<<0x0c,
    TOK_FLATTEN  =1<<0x0d,
    TOK_EXPRS    =TOK_WS | TOK_NOTE | TOK_REDUCE | TOK_FLATTEN,

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

void show_tok(TOK *tok);
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
    if (tok->flags&TOK_NOTE)    fprintf(ofile,"NOTE " );
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

void show_tok(TOK *tok) {
    show_tok_flags(stdout,tok);
    print_ltvs(&tok->ltvs,1);
    fflush(stdout);
}

void show_toks(char *pre,CLL *toks,char *post)
{
    void *op(CLL *lnk) { show_tok((TOK *) lnk); show_toks("(",&((TOK *) lnk)->subtoks,") "); return NULL; }
    if (pre) printf("%s",pre);
    CLL_map(toks,FWD,op);
    if (post) printf("%s",post);
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
    int i=0,d=0;
    FILE *dumpfile;

    void graph_ltvs(CLL *ltvs) {
        fprintf(dumpfile,"\"%x\" [label=\"\" shape=point color=red]\n",ltvs);
        fprintf(dumpfile,"\"%x\" -> \"%x\" [color=red]\n",ltvs,ltvs->lnk[0]);
    }

    void graph_lti(LTI *lti,int depth,int *flags) {
        fprintf(dumpfile,"\"%x\" [label=\"%s\" shape=ellipse]\n",lti,lti->name);
        if (rb_parent(&lti->rbn)) fprintf(dumpfile,"\"%x\" -> \"%x\" [color=blue]\n",rb_parent(&lti->rbn),&lti->rbn);
        fprintf(dumpfile,"\"%x\" -> \"%x\" [weight=2]\n",&lti->rbn,&lti->ltvs);
        graph_ltvs(&lti->ltvs);
    }

    void graph_ltvr(LTVR *ltvr,int depth,int *flags) {
        if (ltvr->ltv) fprintf(dumpfile,"\"%x\" -> \"%x\" [weight=2]\n",ltvr,ltvr->ltv);
        fprintf(dumpfile,"\"%x\" [label=\"\" shape=point color=brown]\n",&ltvr->lnk);
        fprintf(dumpfile,"\"%x\" -> \"%x\" [color=brown]\n",&ltvr->lnk,ltvr->lnk.lnk[0]);
    }

    void graph_ltv(LTV *ltv,int depth,int *flags) {
        if (ltv->flags&LT_AVIS && (*flags=LT_TRAVERSE_HALT)) return;

        if (ltv->len && !(ltv->flags&LT_NSTR)) {
            fprintf(dumpfile,"\"%x\" [style=filled shape=box label=\"",ltv);
            fstrnprint(dumpfile,ltv->data,ltv->len);
            fprintf(dumpfile,"\"]\n");
        }
        else if ((ltv->flags&LT_IMM)==LT_IMM)
            fprintf(dumpfile,"\"%x\" [label=\"I(%x)\" shape=box style=filled]\n",ltv,ltv->data);
        else if (ltv->flags&LT_NULL)
            fprintf(dumpfile,"\"%x\" [label=\"\" shape=box style=filled]\n",ltv);
        else if (ltv->flags&LT_NIL)
            fprintf(dumpfile,"\"%x\" [label=\"NIL\" shape=box style=filled]\n",ltv);
        else
            fprintf(dumpfile,"\"%x\" [label=\"\" shape=box style=filled height=.1 width=.3]\n",ltv);

        if (ltv->sub.ltis.rb_node)
            //fprintf(dumpfile,"\"%1$x\" -> \"%2$x\" [color=blue lhead=\"cluster_%2$x\"]\n\n",ltv,ltv->sub.ltis.rb_node);
            fprintf(dumpfile,"\"%1$x\" -> \"%2$x\" [color=blue]\n",ltv,ltv->sub.ltis.rb_node);
    }

    void *preop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        //if (*ltv) fprintf(dumpfile,"subgraph cluster_%s_%d { // %d\n",(*ltv)->data,i++,d++);
        if (*lti)       graph_lti(*lti,depth,flags);
        else if (*ltvr) graph_ltvr(*ltvr,depth,flags);
        else if (*ltv)  graph_ltv(*ltv,depth,flags);
        return NULL;
    }

    void *postop(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
        //if (*ltv) fprintf(dumpfile,"} // %d\n",--d);
        return NULL;
    }

    void descend_ltvr(LTVR *ltvr) { listree_traverse(ltvr->ltv,preop,postop); }
    void descend_ltvs(CLL *ltvs) {
        int halt=0;
        graph_ltvs(ltvs);
        void *op(CLL *lnk) { graph_ltvr((LTVR *) lnk,0,&halt); descend_ltvr((LTVR *) lnk); return NULL; }
        CLL_map(ltvs,FWD,op);
    }

    void descend_toks(CLL *toks,char *label) {
        void *op(CLL *lnk) {
            TOK *tok=(TOK *) lnk;
            fprintf(dumpfile,"\"%x\" [label=\"\" shape=box label=\"",tok);
            show_tok_flags(dumpfile,tok);
            fprintf(dumpfile,"\"]\n");
            fprintf(dumpfile,"\"%x\" -> \"%x\" [color=red]\n",tok,lnk->lnk[0]);
            //fprintf(dumpfile,"\"%x\" -> \"%x\"\n",tok,&tok->ltvs);
            fprintf(dumpfile,"\"%2$x\" [label=\"ltvs\"]\n\"%1$x\" -> \"%2$x\"\n",tok,&tok->ltvs);
            descend_ltvs(&tok->ltvs);
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
        descend_ltvs(&context->anons);
        fprintf(dumpfile,"\"D%2$x\" [label=\"Dict\"]\n\"Context %1$x\" -> \"D%2$x\" -> \"%2$x\"\n",context,&context->dict);
        descend_ltvs(&context->dict);
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


#define OPS "$@/!&|=<>(){}"
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
            case '#':  tlen=series(data,len,"#\n",NULL,NULL);           STRY(!append(tok,TOK_EXPR|TOK_NOTE,   data+1,tlen-2,tlen),"appending note");     break;
            case '[':  tlen=series(data,len,NULL,NULL,"[]");            STRY(!append(tok,TOK_EXPR|TOK_VAL,    data+1,tlen-2,tlen),"appending lit");      break;
            case '\'': tlen=series(data,len,NULL,NULL,"\'\'");          STRY(!append(tok,TOK_EXPR|TOK_REDUCE, data+1,tlen-2,tlen),"appending reduce");   break;
            case '\"': tlen=series(data,len,NULL,NULL,"\"\"");          STRY(!append(tok,TOK_EXPR|TOK_FLATTEN,data+1,tlen-2,tlen),"appending flatten");  break;
            case '<': case '>': case '(': case ')': case '{': case '}': STRY(!append(tok,TOK_ATOM,            data  ,     1,   1),"appending %c",*data); break; // special, non-ganging op, no balance!
            default:   tlen=series(data,len,OPS,ATOM_END,NULL);         STRY(!append(tok,TOK_ATOM,            data  ,tlen  ,tlen),"appending atom");     break;
        }
    }
    else if (tok->flags&TOK_ATOM) while (len) {
        TOK *ops=NULL,*ref=tok,*val=NULL;

        if (tlen=series(data,len,OPS,NULL,NULL)) // ops
            STRY(!(ops=append(tok,TOK_OPS,data,tlen,tlen)),"appending ops");

        while (len) {
            if ((tlen=series(data,len,NULL,".[",NULL))) {
                int reverse=advance(data[0]=='-')?TOK_REV:0;
                int wildcard=series(data,len,NULL,"*?",NULL)<tlen?TOK_WC:0;
                if (reverse) tlen-=1;
                STRY(!(ref=append(tok,TOK_REF|reverse|wildcard,data,tlen,tlen)),"appending ref");
            }
            while ((tlen=series(data,len,NULL,NULL,"[]")))
                STRY(!(val=append(ref,TOK_VAL,data+1,tlen-2,tlen)),"appending val");
            if ((tlen=series(data,len,".",NULL,NULL))) {
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

int edict_eval(EDICT *edict)
{
    int status=0;

    int eval_context(CONTEXT *context) {
        int status=0;
        TOK *tok;

        int eval_tok(TOK *tok) {
            int eval_atom(TOK *atom_tok) {
                void *resolve_ref(TOK *ref_tok,int insert) {
                    void *resolve_stackframe(CLL *lnk) {
                        int status=0;
                        TOK *acc_tok=ref_tok;
                        void *descend(LTI **lti,LTVR **ltvr,LTV **ltv,int depth,int *flags) {
                            int status=0;
                            TOK *next_tok=acc_tok; // a little finesse...

                            if (*ltv) {
                                if (acc_tok->ref && acc_tok->ref->lti) {
                                    *lti=acc_tok->ref->lti;
                                } else {
                                    LTV *ref_ltv=NULL;
                                    STRY(!(ref_ltv=LTV_peek(&acc_tok->ltvs,HEAD)),"getting token name");
                                    int inserted=insert && !((*ltv)->flags&LT_RO) && (!(*ltvr) || !((*ltvr)->flags&LT_RO)); // directive on way in, status on way out
                                    STRY(!((*lti)=RBR_find(&(*ltv)->sub.ltis,ref_ltv->data,ref_ltv->len,&inserted)),"looking up name in ltv");
                                    if (acc_tok->ref)
                                        acc_tok->ref->lti=*lti;
                                    else
                                        STRY(!(acc_tok->ref=REF_new(*lti)),"allocating ref");
                                }
                            }
                            else if (*lti) {
                                next_tok=(TOK *) CLL_next(&atom_tok->subtoks,&acc_tok->lnk,FWD);
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

                        if (!lnk) return NULL;
                        LTVR *ltvr=(LTVR *) lnk;
                        TOK *rtok=(TOK *) listree_traverse(ltvr->ltv,descend,NULL);
                        return status?NULL:rtok;
                    }

                    //TOK_freerefs(ref_tok);
                    return CLL_map(&context->dict,FWD,resolve_stackframe);
                }

                void *iterate_ref(TOK *ref_tok) {
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

                int resolve_ops(TOK *tok,char *ops,int opslen) {
                    int status=0;

                    int deref(int getnext) {
                        int status=0;
                        LTV *ltv=NULL;
                        TOK *rtok=NULL;
                        STRY(!tok,"validating tok");
                        rtok=(TOK *) (getnext?iterate_ref(tok):resolve_ref(tok,0));
                        //if
                            STRY((!rtok || !rtok->ref || !(ltv=(LTV *) LTV_peek(&rtok->ref->ltvs,HEAD))),"checking deref results");
                          //  ltv=LTV_NIL;
                        STRY(!LTV_enq(&context->anons,ltv,HEAD),"pushing ltv to anons");
                        done:
                        return status;
                    }

                    atom_tok->flags&=~TOK_RERUN; // discard tok by default

                    for (int i=0;i<opslen;i++) {
                        switch (ops[i]) {
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
                                STRY(!LTV_put(&rtok->ref->lti->ltvs,ltv,rtok->flags&TOK_REV,NULL),"adding anon to lti");
                                break;
                            }
                            case '/': {
                                TOK *rtok=NULL;
                                if (tok) {
                                    TRYCATCH(!(rtok=(TOK *) resolve_ref(tok,0)),0,nothing_to_release,"looking up reference for '/'");
                                    TRYCATCH(!(rtok->ref) || !(rtok->ref->ltvr),0,nothing_to_release,"getting rtok ref ltvr");
                                    LTVR_release(&rtok->ref->ltvr->lnk);
                                    TOK_freeref(rtok);
                                }
                                else
                                    LTV_release(LTV_deq(&context->anons,HEAD));
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
                            case '=': // deep copy
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
                if (status && status!=CONDITIONAL_BAIL) {
                    show_toks("Atom evaluation exception:\n",&atom_tok->subtoks,"");
                    status=0;
                    TOK_free(atom_tok);
                } else if (!(atom_tok->flags&TOK_RERUN)) {
                    TOK_free(atom_tok);
                }
                return status;
            }


            int eval_expr(TOK *tok) {
                int status=0;
                static int viewer=2;

                if (viewer&1)
                    edict_graph(edict);

                if (CLL_EMPTY(&tok->subtoks)) {
                    if (tok->flags&TOK_VAL) {
                        STRY(!LTV_enq(&context->anons,LTV_deq(&tok->ltvs,HEAD),HEAD),"pushing expr lit");
                        TOK_free(tok); // for now
                    }
                    else if (tok->flags&TOK_NOTE)
                        TOK_free(tok); // for now
                    else if (tok->flags&TOK_WS)
                        TOK_free(tok); // for now
                    else if (tok->flags&TOK_REDUCE)
                        TOK_free(tok); // for now
                    else if (tok->flags&TOK_FLATTEN)
                        TOK_free(tok); // for now
                    else if (parse(tok))
                        TOK_free(tok); // empty expr
                }
                else { // evaluate
                    TRY(eval_tok((TOK *) CLL_HEAD(&tok->subtoks)),"evaluating expr subtoks");
                    CATCH(status==CONDITIONAL_BAIL,0,goto bail_out,"conditionally executing expr subtoks (not an error)");
                    SCATCH("evaluating expr subtoks");
                }

                goto done; // success!

                bail_out:
                if (viewer&2)
                    show_toks("Bailing on expression:\n",&tok->subtoks,"");
                TOK *subtok;
                while ((subtok=tokpop(&tok->subtoks))) TOK_free(subtok);

                done:
                if (viewer&4)
                    edict_graph(edict);

                if (CLL_EMPTY(&tok->subtoks))
                    TOK_free(tok); // evaluation done; Could add some kind of sentinel atom in here to let the expression repopulate itself and continue
                return status;
            }

            int eval_file(TOK *tok) {
                int status=0;
                char *line;
                int len;
                TOK *expr=NULL;
                LTV *tok_data;
                int viewer=1;

                STRY(!tok,"validating file tok");
                STRY(!(tok_data=LTV_peek(&tok->ltvs,HEAD)),"validating file");
                if (stdin==(FILE *) tok_data->data) { printf(CODE_BLUE "j2> " CODE_RESET); fflush(stdout); }
                if (viewer&1) edict_graph(edict);
                TRYCATCH((line=balanced_readline((FILE *) tok_data->data,&len))==NULL,TRY_ERR,close_file,"reading from file");
                TRYCATCH(!(expr=TOK_new(TOK_EXPR,LTV_new(line,len,LT_OWN))),TRY_ERR,free_line,"allocating expr tok");
                TRYCATCH(!tokpush(&context->toks,expr),TRY_ERR,free_expr,"enqueing expr token");
                goto done; // success

                free_expr:  TOK_free(expr);
                free_line:  free(line);
                close_file: fclose((FILE *) tok_data->data);
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
    LTV *root=LTV_new("ROOT",TRY_ERR,0);
    LT_init();
    STRY(!edict,"validating arg edict");
    BZERO(*edict);
    STRY(!LTV_enq(CLL_init(&edict->dict),root,HEAD),"pushing edict->dict root");
    CLL_init(&edict->contexts);
    CONTEXT *context=CONTEXT_new(TOK_new(TOK_FILE,LTV_new((void *) stdin,sizeof(FILE *),LT_IMM)));
    STRY(!LTV_enq(&context->dict,root,HEAD),"pushing context->dict root");
    STRY(!CLL_put(&edict->contexts,&context->lnk,HEAD),"pushing edict's initial context");
    STRY(!CLL_init(&tok_repo),"initializing tok_repo");
    STRY(!CLL_init(&ref_repo),"initializing ref_repo");

 done:
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
