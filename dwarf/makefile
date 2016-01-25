CFLAGS= -Wall -I.. -g -ggdb3 -feliminate-dwarf2-dups
myreader : myreader.c ; gcc $(CFLAGS) ../util.c myreader.c -o myreader -ldwarf -lelf
clean: ; rm -f myreader
