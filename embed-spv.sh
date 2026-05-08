#!/bin/sh
# embed-spv.sh — convert SPIR-V binaries to a single C++ header
# Usage: embed-spv.sh <out.h> <NAME1> <file1.spv> [<NAME2> <file2.spv> ...]
set -e
out="$1"; shift
{
  echo "#pragma once"
  echo "#include <cstdint>"
  while [ $# -ge 2 ]; do
    name="$1"; file="$2"; shift 2
    echo "static constexpr uint32_t ${name}[] = {"
    od -An -tx4 -v "$file" | awk '{for(i=1;i<=NF;i++) printf "  0x%s,\n", $i}'
    echo "};"
  done
} > "$out"
