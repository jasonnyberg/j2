#include "test.h"

typedef struct {int a; int b; } Collision;

Collision collision1;


int q=12;
volatile int x=0;
const int const_int=10;
const double const_double=10.0;
int y[10] = {1,2,3,4,5,6,7,8,9,10};
int *z=&q;

teststruct w;
testarray testarrayvar;
teststring teststringvar;
char *charpointer="Hello, world!";

testarray ta;

extern int testfunc(int arga, teststruct *argb, testenum argc, teststruct argd)
{
    int localc = 5;
    int locale;
    z=&localc;

    {
        int locald = MYMACRO(localc) + const_int + ta[10] + *z + q;
        locale=locald;
    }

    return x+y[5]+*z+arga+argb->b+localc+locale*const_double;
}

extern int add(int a,int b) { return a+b; }

extern void voidfunc(int a,int b) { x=a+b; }
