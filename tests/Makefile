include ../local.mk
include ../common.mk

CATCH2_PREFIX			= ../lib/libbio/lib/Catch2
CATCH2_HEADERS			= $(shell find $(CATCH2_PREFIX)/include)

CPPFLAGS += -I$(CATCH2_PREFIX)/single_include
CXXFLAGS += -coverage
LDFLAGS += -coverage

OBJECTS	=	bgzip_reader.o \
			bgzip_reverse_msa_reader.o \
			main.o

TEST_FILES =	test-files/random-200000B.txt \
				test-files/equal-length-1/1 \
				test-files/equal-length-1/2 \
				test-files/equal-length-1/3 \
				test-files/equal-length-1/4

GCDA = $(OBJECTS:.o=.gcda)
GCNO = $(OBJECTS:.o=.gcno)


LIBRARIES = ../libfoundergraphs/libfoundergraphs.a


all: run-tests


.PHONY: run-tests clean clean-tests


run-tests: tests $(TEST_FILES)
	./tests
	#lcov --capture --directory . --directory ../combine-msa-vcf --output-file test-coverage.covdata --include '*/combine-msa-vcf/*'
	#genhtml --legend --title test-coverage -o test-coverage test-coverage.covdata


tests: $(OBJECTS) $(LIBRARIES)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(LIBRARIES) $(BOOST_LIBS)


clean: clean-tests
	$(RM) $(OBJECTS) $(GCDA) tests


clean-tests:
	$(RM) $(GCNO) test-coverage.covdata
	$(RM) -r test-coverage


$(CATCH2_PREFIX)/single_include/catch2/catch.hpp: $(CATCH2_HEADERS)
	cd $(CATCH2_PREFIX) && $(PYTHON) scripts/generateSingleHeader.py


# FIXME: make these more generic.
test-files/random-200000B.txt: test-files/random-200000B.txt.gz
	$(GZIP) -d -c $< > $@

test-files/equal-length-1/1: test-files/equal-length-1/1.gz
	$(GZIP) -d -c $< > $@

test-files/equal-length-1/2: test-files/equal-length-1/2.gz
	$(GZIP) -d -c $< > $@

test-files/equal-length-1/3: test-files/equal-length-1/3.gz
	$(GZIP) -d -c $< > $@

test-files/equal-length-1/4: test-files/equal-length-1/4.gz
	$(GZIP) -d -c $< > $@
