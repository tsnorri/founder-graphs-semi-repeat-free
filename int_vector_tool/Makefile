include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: int_vector_tool

clean:
	$(RM) $(OBJECTS) int_vector_tool cmdline.c cmdline.h version.h

int_vector_tool: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
