# Copyright 2015 Plexxi Inc. and its licensors.  All rights reserved.
# Use and duplication of this software is subject to a separate license
# agreement between the user and Plexxi or its licensor.
include ../Vars.mk

PACKAGE_NAME = px-j2
PACKAGE_DEPENDS  = lsb-base (>= 3.2-14)
PACKAGE_DESCR = Edict interpreter

CFLAGS += -Wno-extra -Wno-error
CFLAGS += -I.
CFLAGS += -fno-strict-aliasing -std=gnu99
CFLAGS += -rdynamic -fPIC -shared -ggdb3 -gdwarf-4 -O3 -fdebug-types-section -fno-eliminate-unused-debug-types
#CFLAGS += -fdebug-prefix-map=${CURRENT_DIR}=.

LDFLAGS += -L/opt/plexxi/bin -L. -Wl,-fno-eliminate-unused-debug-types

TARGET = jj
SRCS   = jj.c
#LIBS   = $(INSTALL_DIR)/libreflect.so -ldwarf -lelf -ldw -ldl -lffi -lpthread
LIBS   = -Wl,--whole-archive $(INSTALL_DIR)/libreflect.so -Wl,--no-whole-archive -lelf -ldw -ldl -lffi -lpthread
INSTALL_DIR = $(DEB_DIR)/opt/plexxi/bin


SO_TARGET   = libreflect.so
SO_SRCS = trace.c util.c cll.c rbtree.c listree.c reflect.c compile.c vm.c extensions.c
SO_INSTALL_DIR = $(INSTALL_DIR)

include $(PLEXXI_TOP)/Rules.mk
