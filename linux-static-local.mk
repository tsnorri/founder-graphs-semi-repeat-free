# Build a mostly static binary.
  
LLVM_ROOT				= /usr/lib/llvm-12
CLANG_INCLUDE_DIR		= $(LLVM_ROOT)/lib/clang/12.0.0/include

CC						= clang-12
CXX						= clang++-12
LIBDISPATCH_CFLAGS		= -U__STDC_HOSTED__ -isystem $(CLANG_INCLUDE_DIR)
LIBDISPATCH_CXXFLAGS	= -U__STDC_HOSTED__ -isystem $(CLANG_INCLUDE_DIR)

CPPFLAGS				= -DBOOST_STACKTRACE_USE_ADDR2LINE
CFLAGS					= -fblocks -U__STDC_HOSTED__ -isystem $(CLANG_INCLUDE_DIR)
CXXFLAGS				= -fblocks -U__STDC_HOSTED__ -isystem $(CLANG_INCLUDE_DIR)

BOOST_LIBS				= -L$(BOOST_ROOT)/lib -lboost_iostreams
LDFLAGS					= -static-libstdc++ -static-libgcc -ldl

# Used in .tar.gz name.
TARGET_TYPE				= static

OPT_FLAGS				= -O2 -g
