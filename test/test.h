typedef int testint;

typedef enum testenum { a,b,c } testenum;

typedef int testarray[100];
typedef char teststring[256];

typedef struct teststruct { char a; int b; testint c; int d:1; int e:2; int f:4; int g:8; int h:16; } teststruct;

extern int testfunc(int arga, teststruct *argb, testenum argc, teststruct argd);

#define MYMACRO(X) (X)*(X)
