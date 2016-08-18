all: compilerun

build: ; mkdir build
cmake: build; cd build && cmake ..
compile: cmake; make -C build
clean: cmake; make -C build clean
run: ; ./jj
compilerun: compile run
