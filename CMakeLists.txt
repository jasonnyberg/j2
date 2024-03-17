cmake_minimum_required(VERSION 2.6)

project(reflect LANGUAGES CXX)

add_definitions(-fpermissive)
add_definitions(-ggdb3)
add_definitions(-fdebug-types-section)
add_definitions(-fno-eliminate-unused-debug-types)
add_definitions(-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=)
add_definitions(-ftls-model=local-dynamic)

include_directories(.)
link_directories(${CMAKE_SOURCE_DIR}/build)

#add_definitions(-O3)
#add_definitions(-gdwarf-4)

# build a shared library that is (no longer) also an executable(!!)
add_library(${PROJECT_NAME} SHARED trace.cc util.cc cll.cc listree.cc reflect.cc compile.cc vm.cc extensions.cc)
target_compile_options(${PROJECT_NAME} PUBLIC "-fPIC")
#target_link_libraries(${PROJECT_NAME} "-pie -Wl,-E")

target_link_libraries(${PROJECT_NAME} dwarf elf dw dl ffi pthread)

#add_definitions(-DLTTNG)
#target_link_libraries(${PROJECT_NAME} urcu urcu-bp urcu-common lttng-ust lttng-ctl lttng-ust-tracepoint)

# build a demo executable that dynamically links reflection as a plugin
add_executable(jj jj_plugin.cc)
set(DIRECT_VM "TRUE")
if (DIRECT_VM)
    add_definitions(-DDIRECT_VM)
    target_link_libraries(jj reflect)
else()
    target_link_libraries(jj dl)
endif()

install(TARGETS ${PROJECT_NAME} jj
        RUNTIME DESTINATION bin COMPONENT ${PROJECT_NAME}
        LIBRARY DESTINATION lib COMPONENT ${PROJECT_NAME})

install(FILES jj bootstrap.edict DESTINATION bin)
