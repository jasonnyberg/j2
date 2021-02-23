#include "util.h"

int ut(void) {
    int length=0;
    char *line=NULL;
    while (line=balanced_readline(stdin,"[{","]}",&length))
	printf("-> %d : %s\n",length,line);
}

