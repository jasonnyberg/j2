add_library(reflect listree.c util.c rbtree.c)

add_executable(edict edict.c)

target_link_libraries(edict reflect -lc)