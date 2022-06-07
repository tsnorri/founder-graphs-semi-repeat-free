include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: find_in_graph

clean:
	$(RM) $(OBJECTS) find_in_graph cmdline.c cmdline.h version.h

find_in_graph: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
