include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: build_cst

clean:
	$(RM) $(OBJECTS) build_cst cmdline.c cmdline.h version.h

build_cst: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
