cmake_minimum_required(VERSION 2.6)

project(j2 LANGUAGES C)

add_definitions(-std=gnu99)
add_definitions(-ggdb3)
add_definitions(-fdebug-types-section)
add_definitions(-fno-eliminate-unused-debug-types)
add_definitions(-DLTTNG)
add_definitions(-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=)

include_directories(.)

#add_definitions(-O3)
#add_definitions(-gdwarf-4)

add_library(reflect SHARED trace.c util.c cll.c listree.c reflect.c compile.c vm.c extensions.c)
add_executable(jj jj.c)
target_link_libraries(jj reflect dwarf elf dw dl ffi pthread urcu urcu-bp urcu-common lttng-ust lttng-ctl lttng-ust-tracepoint)

set_property(TARGET reflect jj PROPERTY POSITION_INDEPENDENT_CODE ON)

install(TARGETS reflect jj
        RUNTIME DESTINATION bin COMPONENT j2
        LIBRARY DESTINATION lib COMPONENT j2)

install(FILES jj.sh bootstrap.edict DESTINATION bin)
