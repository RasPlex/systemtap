#!/bin/bash

# Extract the dynamic symbols from the main binary, there is no need
# to also have these in the normal symbol table
nm -D binary --format=posix --defined-only | awk '{ print $1 }' | sort > binary.dynsyms

# Extract all the text (i.e. function) symbols from the debuginfo .
nm binary --format=posix --defined-only | awk '{ if ($2 == "T" || $2 == "t" || $2 == "D") print $1 }' | sort > binary.funcsyms

# Keep all the function symbols not already in the dynamic symbol
# table.
comm -13 binary.dynsyms binary.funcsyms > binary.keep_symbols

# We don't have a split executable in advance
# so we need to strip the binary ourselves
strip --strip-all -o binary.strip binary

# Separate full debug info into debug binary.
objcopy --only-keep-debug binary binary.debug

# Copy the full debuginfo, keeping only a minimal set of symbols and
# removing some unnecessary sections.
rm -f binary.mini_debuginfo
objcopy -S --remove-section .gdb_index --remove-section .comment --keep-symbols=binary.keep_symbols binary.debug binary.mini_debuginfo

#compress the binary.mini_debuginfo while keeping original file
rm -f binary.mini_debuginfo.xz
xz -k binary.mini_debuginfo

# Inject the compressed data into the .gnu_debugdata section of the
# original binary
objcopy --add-section .gnu_debugdata=binary.mini_debuginfo.xz binary.strip binary.test
