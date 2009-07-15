#include "listree.h"

#define LIT_DELIMIT WHITESPACE "'[(){}<>"

enum { DELIMIT_SIMPLE_LIT_END, DELIMIT_EXP_START, DELIMIT_EXP_END, DELIMIT_MAX };

typedef struct EDICT
{
    CLL code;
    CLL anon;
    CLL dict;
    int numbc;
    char bc[256];
} EDICT;

typedef int (*edict_bc_impl)(struct EDICT *edict,char *name,int len);
int numbc;
char edict_bc[256];
edict_bc_impl edict_bcf[256];

//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

extern int edict_init(EDICT *edict,LTV *root);
extern int edict_destroy(EDICT *edict);
extern int edict_repl(EDICT *edict);

extern LTV *edict_add(EDICT *edict,LTV *ltv,void *metadata);
extern LTV *edict_rem(EDICT *edict,void **metadata);
extern LTV *edict_name(EDICT *edict,char *name,int len,int end,void *metadata);
extern LTV *edict_ref(EDICT *edict,char *name,int len,int pop,int end,void *metadata);

/*
extern LTV *edict_add(RBR *rbr,LTV *ltv);

extern LTV *edict_push(RBR *rbr,char *buf,int len);
extern LTV *edict_strpush(RBR *rbr,char *str);

extern void *edict_pop(RBR *rbr,char *name,int len);
extern void *edict_strpop(RBR *rbr,char *name);

extern void edict_nameitem(LTV *ltv,char *name,int len);
extern void edict_name(RBR *rbr,char *name,int len);
extern void edict_rename(RBR *rbr,char *old,int oldlen,char *new,int newlen);

extern LTV *edict_clone(LTV *ltv,int sibs);
extern int edict_copy_item(RBR *rbr,LTV *ltv);
extern int edict_copy(RBR *rbr,char *name,int len);
extern int edict_raise(RBR *rbr,char *name,int len);

extern void *edict_lookup(RBR *rbr,char *name,int len);

extern void edict_display_item(LTV *ltv,char *prefix);
extern void edict_list(RBR *rbr,char *buf,int len,int count,char *prefix);
extern void edict_strlist(RBR *rbr,char *str,int count,char *prefix);

extern int edict_len(RBR *rbr,char *buf,int len);
extern int edict_strlen(RBR *rbr,char *str);

extern LTV *edict_getitem(RBR *rbr,char *name,int len,int pop);
extern LTV *edict_getitems(RBR *rbr,LTV *repos,int display);

extern LTV *edict_get_nth_item(RBR *rbr,int n);

*/
