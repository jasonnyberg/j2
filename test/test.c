volatile int x=0;
const int const_int=10;
const double const_double=10.0;
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

testarray ta;

extern int testfunc(int arga, teststruct *argb, testenum argc, teststruct argd)
{
    int localc = 5;
    int locale;
    z=&localc;

    {
        int locald = MYMACRO(localc) + const_int + ta[10];
        locale=locald;
    }

    return x+y[5]+*z+arga+argb->b+localc+locale*const_double;
}

int main(int argc,char *argv[])
{
    teststruct ts;
    testfunc(1,&ts,a,ts);
}
