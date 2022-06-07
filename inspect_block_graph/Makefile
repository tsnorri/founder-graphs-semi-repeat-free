include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: inspect_block_graph

clean:
	$(RM) $(OBJECTS) inspect_block_graph cmdline.c cmdline.h version.h

inspect_block_graph: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
