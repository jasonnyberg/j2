SHELL:=bash
.SHELLFLAGS:=-eu -o pipefail -c
.ONESHELL:=true
.DELETE_ON_ERROR:=true


all: compile

switch-compiler:; sudo update-alternatives --config c++
build:; mkdir build
cmake: build; cd build && cmake ..
compile: cmake; make -C build
clean: cmake; make -C build clean
distclean: ; rm -rf build
run: compile; MALLOC_CHECK=1 ./jj.sh
compilerun: compile run
install: cmake; sudo make -C build install
fastbench: cmake; rm callgrind.out.*; echo "fastbench!" | (valgrind --tool=callgrind build/jj)
midbench: cmake; rm callgrind.out.*; echo "midbench!" | (valgrind --tool=callgrind build/jj)
inspect:; kcachegrind callgrind.out.*
readelf:; readelf -a build/libreflect.so
dwarfdump:; dwarfdump -G -i -d build/libreflect.so
codeblocks: build; pushd build; cmake -G"CodeBlocks - Unix Makefiles" ..; popd

REFLECT_FLAGS+= --shared -ggdb3 -fno-eliminate-unused-debug-types

test/testh.so: ; gcc $(REFLECT_FLAGS) -o $@ test/testh.c
test/test.so:  ; gcc $(REFLECT_FLAGS) -o $@ test/test.c
test/math.so:  ; gcc $(REFLECT_FLAGS) -o $@ test/math.c

build/libreflect.dbg: build/libreflect.so
	objcopy --only-keep-debug $^ $@
	objcopy --strip-debug $^
	objcopy --add-gnu-debuglink=$@ $^

strip: compile build/libreflect.dbg
split:; make -f Make.splitdwarf
