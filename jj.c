#include "rbtree.h"
#include "listree.h"
#include "util.h"

struct rb_root *root;


/* temporary until reflect works */
void *lt_dump_ltv(CLL *lnk,void *data)
{
    printf("  %s%s\n",(char *) data,LTVDATA((LTV *) lnk));
    return NULL;
}

void *lt_dump_rbn(LTN *rbn,void *data)
{
    printf("%s%s:\n",(char *) data,LTINAME(rbn));
    return CLL_traverse(&LTICLL(rbn),0,lt_dump_ltv,data);
}

void *lt_dump(LTR *ltr,void *data)
{
    return lt_traverse(ltr,lt_dump_rbn,data);
}


int main()
{
    LTI *lti;
    root=NEW(struct rb_root);

    lti=lt_get(root,"aaa",1);
    CLL_put(&LTICLL(lti),&LTVLNK(ltv_new("123")),0);
    lti=lt_get(root,"bbb",1);
    CLL_put(&LTICLL(lti),&LTVLNK(ltv_new("456")),0);
    lti=lt_get(root,"aaa",1);
    CLL_put(&LTICLL(lti),&LTVLNK(ltv_new("789")),0);
    lti=lt_get(root,"aaa",1);
    CLL_put(&LTICLL(lti),&LTVLNK(ltv_new("abc")),1);
    
    lt_dump(root,NULL);
}

