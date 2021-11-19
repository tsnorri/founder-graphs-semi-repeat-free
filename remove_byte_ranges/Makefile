include ../local.mk
include ../common.mk

OBJECTS		=	cmdline.o \
				main.o

all: remove_byte_ranges

clean:
	$(RM) $(OBJECTS) remove_byte_ranges cmdline.c cmdline.h version.h

remove_byte_ranges: $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

main.cc : cmdline.c
cmdline.c : config.h

include ../config.mk
