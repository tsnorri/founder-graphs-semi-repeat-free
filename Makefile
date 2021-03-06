include local.mk
include common.mk

BUILD_PRODUCTS =	build_cst/build_cst \
					build_founder_graph_index/build_founder_graph_index \
					build_msa_index/build_msa_index \
					find_founder_block_boundaries/find_founder_block_boundaries \
					find_in_graph/find_in_graph \
					founder_block_tool/founder_block_tool \
					founder_graph_tool/founder_graph_tool \
					inspect_block_graph/inspect_block_graph \
					int_vector_tool/int_vector_tool \
					msa_index_cmp/msa_index_cmp \
					optimize_segmentation/optimize_segmentation \
					remove_byte_ranges/remove_byte_ranges

LIBBIO_DEPENDENCIES = lib/libbio/src/libbio.a
ifeq ($(shell uname -s),Linux)
	LIBBIO_DEPENDENCIES += lib/swift-corelibs-libdispatch/build/src/libdispatch.a
endif


# “$() $()” is a literal space.
OS_NAME = $(shell tools/os_name.sh)
VERSION = $(subst $() $(),-,$(shell tools/git_version.sh))
DIST_TARGET_DIR = founder-graphs-semi-repeat-free-$(VERSION)
DIST_NAME_SUFFIX = $(if $(TARGET_TYPE),-$(TARGET_TYPE),)
DIST_TAR_GZ = founder-graphs-semi-repeat-free-$(VERSION)-$(OS_NAME)$(DIST_NAME_SUFFIX).tar.gz


all: $(BUILD_PRODUCTS)

clean:
	$(MAKE) -C libfoundergraphs clean
	$(MAKE) -C build_cst clean
	$(MAKE) -C build_founder_graph_index clean
	$(MAKE) -C build_sa clean
	$(MAKE) -C build_msa_index clean
	$(MAKE) -C find_founder_block_boundaries clean
	$(MAKE) -C find_in_graph clean
	$(MAKE) -C founder_block_tool clean
	$(MAKE) -C founder_graph_tool clean
	$(MAKE) -C inspect_block_graph clean
	$(MAKE) -C int_vector_tool clean
	$(MAKE) -C msa_index_cmp clean
	$(MAKE) -C optimize_segmentation clean

clean-all: clean
	$(MAKE) -C lib/libbio clean

dist: $(DIST_TAR_GZ)

$(DIST_TAR_GZ): $(BUILD_PRODUCTS)
	$(MKDIR) -p $(DIST_TARGET_DIR)
	$(CP) $(BUILD_PRODUCTS) $(DIST_TARGET_DIR)
	$(CP) README.md $(DIST_TARGET_DIR)
	$(CP) LICENSE $(DIST_TARGET_DIR)
	$(CP) lib/swift-corelibs-libdispatch/LICENSE $(DIST_TARGET_DIR)/swift-corelibs-libdispatch-license.txt
	$(CP) lib/cereal/LICENSE $(DIST_TARGET_DIR)/cereal-license.txt
	$(TAR) czf $(DIST_TAR_GZ) $(DIST_TARGET_DIR)
	$(RM) -rf $(DIST_TARGET_DIR)

libfoundergraphs/libfoundergraphs.a: $(LIBBIO_DEPENDENCIES)
	$(MAKE) -C libfoundergraphs

build_cst/build_cst: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C build_cst

build_founder_graph_index/build_founder_graph_index: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C build_founder_graph_index

build_msa_index/build_msa_index: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C build_msa_index

build_sa/build_sa: lib/parallel-divsufsort/build/divsufsort.a
	$(MAKE) -C build_sa

find_founder_block_boundaries/find_founder_block_boundaries: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C find_founder_block_boundaries

find_in_graph/find_in_graph: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C find_in_graph

founder_block_tool/founder_block_tool: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C founder_block_tool

founder_graph_tool/founder_graph_tool: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C founder_graph_tool

inspect_block_graph/inspect_block_graph: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C inspect_block_graph

int_vector_tool/int_vector_tool: $(LIBBIO_DEPENDENCIES)
	$(MAKE) -C int_vector_tool

msa_index_cmp/msa_index_cmp: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C msa_index_cmp

optimize_segmentation/optimize_segmentation: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C optimize_segmentation

remove_byte_ranges/remove_byte_ranges: libfoundergraphs/libfoundergraphs.a
	$(MAKE) -C remove_byte_ranges

lib/libbio/local.mk: local.mk
	$(CP) local.mk lib/libbio

lib/libbio/src/libbio.a: lib/libbio/local.mk
	$(MAKE) -C lib/libbio/src

lib/parallel-divsufsort/build/divsufsort.a:
	cd lib/parallel-divsufsort && mkdir -p build && cd build && $(CMAKE) -DOPENMP=ON -DCILKP=OFF -DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_COMPILER_IS_GNUCXX:INTEGER=1 .. && $(MAKE) libprange divsufsort

lib/swift-corelibs-libdispatch/build/src/libdispatch.a:
	$(RM) -rf lib/swift-corelibs-libdispatch/build && \
	cd lib/swift-corelibs-libdispatch && \
	$(MKDIR) build && \
	cd build && \
	$(CMAKE) \
		-G Ninja \
		-DCMAKE_C_COMPILER="$(CC)" \
		-DCMAKE_CXX_COMPILER="$(CXX)" \
		-DCMAKE_C_FLAGS="$(LIBDISPATCH_CFLAGS)" \
		-DCMAKE_CXX_FLAGS="$(LIBDISPATCH_CXXFLAGS)" \
		-DBUILD_SHARED_LIBS=OFF \
		.. && \
	$(NINJA) -v

