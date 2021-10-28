# Tools for Generating Semi-Repeat-Free Founder Graphs

This repository contains a set of tools for generating semi-repeat-free founder graphs (submitted for peer review).

> **_NOTE:_** Work-in-progress. Currently only a segmentation can be generated. Generating a founder graph index has not been implemented.

## Build/Runtime Requirements

On Linux, [libbsd](https://libbsd.freedesktop.org/) is required.

## Build Requirements

- [Clang++](https://clang.llvm.org) with C++20 support
- [GNU gengetopt](https://www.gnu.org/software/gengetopt/gengetopt.html) (tested with version 2.22.6)
- [Ragel State Machine Compiler](http://www.colm.net/open-source/ragel/) is required to build the bundled libbio (tested with version 6.8).
- [CMake](http://cmake.org) for building libdispatch on Linux
- [Ninja](https://ninja-build.org) for building libdispatch on Linux
- [Boost](http://www.boost.org)

All other dependencies are included in the repository as submodules.

## Building

### Short Version

1. `git clone --recursive https://github.com/tsnorri/founder-graphs-semi-repeat-free.git`
2. `cd founder-graphs-semi-repeat-free`
3. `cp linux-static.local.mk local.mk`
4. Edit local.mk
5. `make -j16`

### Long Version

Currently some configuration may need to be done manually by editing `local.mk` as described below.

1. Clone the repository with `git clone --recursive https://github.com/tsnorri/founder-graphs-semi-repeat-free.git`
2. Change the working directory with `cd founder-graphs-semi-repeat-free`.
3. Create the file `local.mk`. `linux-static.local.mk` is provided as an example and may be copied with `cp linux-static.local.mk local.mk`
4. Edit `local.mk` in the repository root to override build variables. In addition to GNU Make’s built-in variables, some other ones listed in `common.mk` are used. Useful variables include `CC`, `CXX`, `RAGEL` and `GENGETOPT` for C and C++ compilers, Ragel and gengetopt respectively. `BOOST_INCLUDE` is used as preprocessor flags when Boost is required. `BOOST_LIBS` is passed to the linker.
5. Run make with a suitable numer of parallel jobs, e.g. `make -j16`

## Generating Semi-Repeat-Free Founder Graphs

Our algorithm takes a multiple sequence alignment as its input and ultimately produces a segmentation from which a semi-repeat-free founder graph may be generated. Please note that not all inputs have a semi-repeat-free segmentation. Reference-guided multiple sequence alignments can be generated from variant data with e.g. [vcf2multialign](https://github.com/tsnorri/vcf2multialign).

On a high level, the workflow consists of the following steps:

1. Build a compressed suffix tree from the inputs such that the gap characters have been removed.
2. Build an index for co-ordinate transformations from the original inputs.
3. Run the segmentation algorithm. For *m* sequences with *n* characters in each, this takes *O(mn log m)* time.
4. Optimize the segmentation and generate an index (not implemented).

### Building a Compressed Suffix Tree

Suppose the multiple sequence alignment is stored in a number of text files such that there is one sequence in each file without a trailing newline, and `sequence-list.txt` contains the paths of the files.

1. Remove gaps from the inputs and concatenate them. This can be done with e.g. `cat sequence-list.txt | while read x; do echo "#" >> concatenated.txt; tr -d "-" "${x}" >> concatenated.txt; done`
2. (Optional) Build the suffix array. We have provided a tool that uses [Parallel-DivSufSort](https://github.com/jlabeit/parallel-divsufsort) for this purpose instead of the version of [divsufsort](https://github.com/y-256/libdivsufsort) bundled with [SDSL](https://github.com/xxsds/sdsl-lite). The concatenated input can be processed with e.g. `build_sa --input=concatenated.txt > concatenated.sa`. However, the resulting file needs to be renamed manually so that SDSL can find it in the next step.
3. Build the compressed suffix tree.
   * To use SDSL’s divsufsort, use `build_cst --input=concatenated.txt > concatenated.cst`.
   * To use the suffix array generated in step 2, use `build_cst --input=concatenated.txt --sa=concatenated.sa > concatenated.cst`.
   * Please see `build_cst --help` for additional options.

### Building a Co-Ordinate Transformation Index

In this phase, the original inputs are required. The inputs may be compressed with `bgzip` (part of [htslib](http://www.htslib.org)) or `gzip`. To compress the files so that they can be used as inputs for `finder_block_boundaries`, use a command like `bgzip -i -@ 16 input.txt`.

Suppose `sequence-list-compressed.txt` contains the paths of the compressed sequence files. The index can be generated with e.g. `build_msa_index --sequence-list=sequence-list-compressed.txt --gzip-input > msa-index.dat`.

### Generating a Semi-Repeat-Free Segmentation

The segmentation can be generated with e.g. `find_founder_block_boundaries --sequence-list=input-list-compressed.txt --cst=concatenated.cst --msa-index=msa-index.dat --bgzip-input > segmentation.dat`.
