DWARFDIR = /home/jnyberg/downloads/dwarf-20090716/libdwarf
CFLAGS= -Wall -I$(DWARFDIR) -I.. -g -ggdb3 -feliminate-dwarf2-dups
myreader : myreader.c ; $(CC) $(CFLAGS) ../util.c myreader.c -o myreader -L$(DWARFDIR) -ldwarf -lelf
clean: ; rm -f myreader
