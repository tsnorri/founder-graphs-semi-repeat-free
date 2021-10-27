include ../local.mk
include ../common.mk

LDFLAGS += -lomp

OBJECTS		=	cmdline.o \
				main.o

all: build_sa

clean:
	$(RM) $(OBJECTS) build_sa cmdline.c cmdline.h version.h

build_sa: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a ../lib/parallel-divsufsort/build/lib/libdivsufsort.a ../lib/parallel-divsufsort/build/external/libprange/lib/liblibprange.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
