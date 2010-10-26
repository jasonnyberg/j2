volatile int x=0;
int y[10];
int *z;

typedef int testint;

typedef enum testenum { a,b,c } testenum;

typedef struct teststruct { char a; int b; testint c; int d:1; int e:2; int f:4; int g:8; int h:16; } teststruct;

teststruct w;

static int testfunc(int arga, teststruct *argb, testenum argc)
{
    int localc = 5;

    {
        int locald = localc + 10;
    }

    return x+y[5]+*z+arga+argb->b+localc;
}

main(int argc,char *argv[])
{
}
