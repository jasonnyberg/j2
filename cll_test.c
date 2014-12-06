#include "stdio.h"
#include "cll.h"

typedef struct {
    CLL cll;
    int i;
} node;

#define NODE(lnk) ((node *) lnk)
CLL *dumpnode(CLL *lnk,void *data)
{
    if (!lnk) return NULL;
    if (!(NODE(lnk)->i%10)) printf("%s%d [color=red]\n",(char *) data,NODE(lnk)->i);
    else printf("%s%d\n",(char *) data,NODE(lnk)->i);
    printf("%s%d -> %s%d [color=red]\n",(char *) data,NODE(lnk)->i,(char *) data,NODE(CLL_SIB(lnk,FWD))->i);
    printf("%s%d -> %s%d\n",(char *) data,NODE(lnk)->i,(char *) data,NODE(CLL_SIB(lnk,REV))->i);
    return NULL;
}

#define NODES 20

main() {
    int i;
    node n[NODES];
    node *cut,*get,*put;

    void show(char *prefix) { for (i=0;i<NODES;i++) dumpnode(&n[i].cll,prefix); }
    for (i=0;i<NODES;i++) { n[i].i=i; CLL_init(&n[i].cll); }

    for (i=1;i<10;i++)
        CLL_splice(&n[0].cll,&n[i].cll,TAIL);
    for (i=11;i<20;i++)
        CLL_splice(&n[10].cll,&n[i].cll,TAIL);
    show("a");

    // append list-10 nodes to list-0
    CLL_splice(&n[0].cll,&n[10].cll,TAIL);// tail-insert list-10 to list-0
    show("b");
    //CLL_splice(&n[10].cll,&n[10].cll,FWD); // fwd-(or rev-)yank list-10 head
    cut=(node *) CLL_cut(&n[10].cll); // yank list-10 head
    show("c");
    dumpnode(&cut->cll,"cut");
    get=(node *) CLL_get(&n[0].cll,KEEP,TAIL);
    //dumpnode(&get->cll,"get");

    // extract list-0's head-thru #9 to list-10
    put=(node *) CLL_put(&n[0].cll,&n[10].cll,HEAD); // head-insert list-10 head into list-0
    show("d");
    //dumpnode(&put->cll,"put");
    CLL_splice(&n[10].cll,&n[9].cll,REV); // tail-yank list-10 - #9
    show("e");
}
