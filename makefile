all: compilerun

build: ; mkdir build
cmake: build; cd build && cmake ..
compile: cmake; make -C build
clean: cmake; make -C build clean
run: ; MALLOC_CHECK=1 ./jj
compilerun: compile run
install: cmake; sudo make -C build install
