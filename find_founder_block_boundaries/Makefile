include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: find_founder_block_boundaries

clean:
	$(RM) $(OBJECTS) find_founder_block_boundaries cmdline.c cmdline.h version.h

find_founder_block_boundaries: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
