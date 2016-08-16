volatile int x=0;
int y[10];
int *z;

typedef int testint;

typedef enum testenum { a,b,c } testenum;

typedef int testarray[100];
typedef char teststring[256];

typedef struct teststruct { char a; int b; testint c; int d:1; int e:2; int f:4; int g:8; int h:16; } teststruct;

teststruct w;
testarray testarrayvar;
teststring teststringvar;

#define MYMACRO(X) (X)*(X)

static int testfunc(int arga, teststruct *argb, testenum argc, teststruct argd)
{
    int localc = 5;

    {
        int locald = localc + 10;
    }

    return x+y[5]+*z+arga+argb->b+localc;
}

int main(int argc,char *argv[])
{
    teststruct ts;
    testfunc(1,&ts,a,ts);
}
