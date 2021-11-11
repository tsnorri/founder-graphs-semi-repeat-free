include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: optimize_segmentation

clean:
	$(RM) $(OBJECTS) optimize_segmentation cmdline.c cmdline.h version.h

optimize_segmentation: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) ../libfoundergraphs/libfoundergraphs.a $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
