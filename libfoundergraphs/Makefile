include ../local.mk
include ../common.mk


OBJECTS = reverse_msa_reader.o

all: libfoundergraphs.a

clean:
	$(RM) $(OBJECTS) libfoundergraphs.a

libfoundergraphs.a: $(OBJECTS)
	$(AR) rcs $@ $^
