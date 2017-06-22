cmake_minimum_required(VERSION 2.6)
add_definitions(-std=gnu99 -ggdb3)
include_directories(.)
add_library(reflect SHARED trace.c util.c cll.c rbtree.c listree.c reflect.c)
add_library(edict MODULE edict.c)
add_executable(jj jj.c)
target_link_libraries(jj reflect dwarf elf dl ffi)
install(TARGETS reflect jj edict
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
