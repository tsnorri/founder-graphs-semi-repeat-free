include ../local.mk
include ../common.mk


OBJECTS =	bgzip_reader.o \
			block_graph.o \
			dispatch_concurrent_builder.o \
			index_construction.o \
			msa_reader.o \
			path_index.o \
			reverse_msa_reader.o \
			utility.o

all: libfoundergraphs.a

clean:
	$(RM) $(OBJECTS) libfoundergraphs.a

libfoundergraphs.a: $(OBJECTS)
	$(AR) rcs $@ $^
