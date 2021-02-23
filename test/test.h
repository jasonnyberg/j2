typedef int testint;

typedef enum testenum { a,b,c } testenum;

typedef int testarray[100];
typedef char teststring[256];

typedef struct basicstruct { char a; int b; unsigned c; } basicstruct;

typedef struct teststruct { char a; int b; testint c; int d:1; unsigned e:2; int f:4; unsigned g:8; int h:16; basicstruct i; } teststruct;

int testfunc(int arga, teststruct *argb, testenum argc, teststruct argd);

#define MYMACRO(X) (X)*(X)

typedef struct anonymous_struct_union
{
    struct { int a,b; };
    union { int c,d; };
} anonymous_struct_union;
