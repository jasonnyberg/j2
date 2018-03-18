all: compile

build:; mkdir build
cmake: build; cd build && cmake ..
compile: cmake; make -C build
clean: cmake; make -C build clean

testh.so: ; gcc -g3 --shared -std=gnu99 -ggdb3 -gdwarf-4 -fno-eliminate-unused-debug-types -o $@ testh.c
test.so: ; gcc -g3 --shared -std=gnu99 -ggdb3 -gdwarf-4 -fno-eliminate-unused-debug-types -o $@ test.c
math.so: ; gcc -g3 --shared -std=gnu99 -ggdb3 -gdwarf-4 -fno-eliminate-unused-debug-types -o $@ math.c

libify:
	gcc -c -g -fPIC math.c -o math.o
	gcc -shared -o libmath.so math.o -Wl,--whole-archive /usr/lib/libtommath.a -Wl,--no-whole-archive
