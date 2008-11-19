#include "rbtree.h"
#include "listree.h"
#include "util.h"

struct rb_root *root;

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
    
    lt_dump(root,NULL);
}

