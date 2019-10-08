all: compile

build:; mkdir build
cmake: build; cd build && cmake ..
compile: cmake; make -C build
dwarf: cd build && dwp -e jj; find . -name "*.dwo" -exec rm {} +
clean: cmake; make -C build clean
run:; MALLOC_CHECK=1 ./jj.sh
#compilerun: compile dwarf run
compilerun: compile run
install: cmake; sudo make -C build install
fastbench: cmake; rm callgrind.out.*; echo "fastbench!" | (valgrind --tool=callgrind build/jj)
midbench: cmake; rm callgrind.out.*; echo "midbench!" | (valgrind --tool=callgrind build/jj)
inspect:; kcachegrind callgrind.out.*

test/testh.so: ; gcc -g3 --shared -std=gnu99 -ggdb3 -gdwarf-4 -fno-eliminate-unused-debug-types -o $@ test/testh.c
test/test.so: ; gcc -g3 --shared -std=gnu99 -ggdb3 -gdwarf-4 -fno-eliminate-unused-debug-types -o $@ test/test.c
test/math.so: ; gcc -g3 --shared -std=gnu99 -ggdb3 -gdwarf-4 -fno-eliminate-unused-debug-types -o $@ test/math.c

