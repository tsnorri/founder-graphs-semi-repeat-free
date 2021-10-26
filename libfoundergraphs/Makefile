include ../local.mk
include ../common.mk


OBJECTS =	bgzip_reader.o \
			reverse_msa_reader.o \
			utility.o

all: libfoundergraphs.a

clean:
	$(RM) $(OBJECTS) libfoundergraphs.a

libfoundergraphs.a: $(OBJECTS)
	$(AR) rcs $@ $^
