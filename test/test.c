volatile int x=0;
int y[10];
int *z;

typedef int testint;

typedef enum testenum { a,b,c } testenum;

typedef struct teststruct { char a; int b; testint c;} teststruct;

teststruct w;

static int testfunc(int arga, teststruct *argb, testenum argc)
{
    int localc = 5;

    {
        int locald = localc + 10;
    }

    return x+y[5]+*z+arga+argb->b+localc;
}