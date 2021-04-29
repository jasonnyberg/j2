cmake_minimum_required(VERSION 2.6)

project(reflect LANGUAGES CXX)

add_definitions(-fpermissive)
add_definitions(-ggdb3)
add_definitions(-fdebug-types-section)
add_definitions(-fno-eliminate-unused-debug-types)
add_definitions(-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=)
add_definitions(-ftls-model=local-dynamic)

include_directories(.)

#add_definitions(-O3)
#add_definitions(-gdwarf-4)

# build a shared library that is also an executable(!!)
add_library(${PROJECT_NAME} SHARED trace.cc util.cc cll.cc listree.cc reflect.cc compile.cc vm.cc extensions.cc jj.cc)
target_compile_options(${PROJECT_NAME} PUBLIC "-fPIC")
target_link_libraries(${PROJECT_NAME} "-pie -Wl,-E")

target_link_libraries(${PROJECT_NAME} dwarf elf dw dl ffi pthread)

#add_definitions(-DLTTNG)
#target_link_libraries(${PROJECT_NAME} urcu urcu-bp urcu-common lttng-ust lttng-ctl lttng-ust-tracepoint)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin COMPONENT ${PROJECT_NAME}
        LIBRARY DESTINATION lib COMPONENT ${PROJECT_NAME})

install(FILES jj.sh bootstrap.edict DESTINATION bin)
