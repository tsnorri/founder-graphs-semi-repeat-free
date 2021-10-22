include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: build_msa_index

clean:
	$(RM) $(OBJECTS) build_msa_index cmdline.c cmdline.h version.h

build_msa_index: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
