include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: msa_index_cmp

clean:
	$(RM) $(OBJECTS) msa_index_cmp cmdline.c cmdline.h version.h

msa_index_cmp: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
