add_library (edict listree.c util.c rbtree.c)
add_executable (jj jj.c)
target_link_libraries (jj edict) 
