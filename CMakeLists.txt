cmake_minimum_required(VERSION 2.6)
#add_definitions(-fPIC -std=gnu99 -ggdb3 -gdwarf-4 -fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.)
add_definitions(-fPIC -std=gnu99 -ggdb3 -gdwarf-4)
add_definitions(-fdebug-types-section -fno-eliminate-unused-debug-types)
include_directories(.)
set (CMAKE_SHARED_LINKER_FLAGS "-Wl,-fno-eliminate-unused-debug-types")
add_library(reflect SHARED trace.c util.c cll.c rbtree.c listree.c reflect.c compile.c vm.c extensions.c)
add_executable(jj jj.c)
target_link_libraries(jj reflect dwarf elf dw dl ffi pthread)
install(TARGETS reflect jj
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
