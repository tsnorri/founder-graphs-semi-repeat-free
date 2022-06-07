include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: founder_graph_tool

clean:
	$(RM) $(OBJECTS) founder_graph_tool cmdline.c cmdline.h version.h

founder_graph_tool: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
