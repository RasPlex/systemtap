#!/bin/bash

# Extract the dynamic symbols from the main binary, there is no need
# to also have these in the normal symbol table
nm -D binary --format=posix --defined-only | awk '{ print $1 }' | sort > dynsyms

# Extract all the text (i.e. function) symbols from the debuginfo .
nm binary --format=posix --defined-only | awk '{ if ($2 == "T" || $2 == "t") print $1 }' | sort > funcsyms

# Keep all the function symbols not already in the dynamic symbol
# table.
comm -13 dynsyms funcsyms > keep_symbols

# Copy the full debuginfo, keeping only a minimal set of symbols and
# removing some unnecessary sections.
objcopy -S --remove-section .gdb_index --remove-section .comment --keep-symbols=keep_symbols binary mini_debuginfo

# Drop the DWARF debuginfo to ensure we're only using minidebuginfo symbol table magic
strip -s binary

# Inject the compressed data into the .gnu_debugdata section of the
# original binary.
rm -f mini_debuginfo.xz
xz mini_debuginfo
objcopy --add-section .gnu_debugdata=mini_debuginfo.xz binary
