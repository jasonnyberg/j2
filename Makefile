all: compile

build: ; mkdir build
cmake: build; cd build && cmake ..
compile: cmake; make -C build
