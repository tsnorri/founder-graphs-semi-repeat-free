include common.mk

BUILD_PRODUCTS =	build_cst/build_cst \
					build_msa_index/build_msa_index \
					find_founder_block_boundaries/find_founder_block_boundaries


all: $(BUILD_PRODUCTS)

clean:
	$(MAKE) -C libfoundergraphs clean
	$(MAKE) -C build_cst clean
	$(MAKE) -C build_msa_index clean
	$(MAKE) -C find_founder_block_boundaries clean

clean-all: clean
	$(MAKE) -C lib/libbio clean

libfoundergraphs/libfoundergraphs.a: lib/libbio/src/libbio.a
	$(MAKE) -C libfoundergraphs

build_cst/build_cst: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C build_cst

build_msa_index/build_msa_index: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C build_msa_index

find_founder_block_boundaries/find_founder_block_boundaries: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C find_founder_block_boundaries

lib/libbio/local.mk: local.mk
	$(CP) local.mk lib/libbio

lib/libbio/src/libbio.a: lib/libbio/local.mk
	$(MAKE) -C lib/libbio/src
