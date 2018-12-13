#!/usr/bin/env bash

# Usage: ./run.sh [-b] <c source>
# With -b, it will rebuild libsafe_rt and LLVMCPI

# Note: llvm built binaries can be in LLVM_BIN env variable

[[ -z $LLVM_BIN ]] || export PATH="$LLVM_BIN:$PATH"

if [[ "$1" = "-b" ]]; then
  # Rebuild libraries
  cwd=`pwd`
  cd ../build
  make -j4
  cd $cwd
  shift
fi

case `uname -s` in
  Linux)   dll=so;;
  Darwin)  dll=dylib;;
  *)       exit 1;;
esac

src=$1
name=${src%.*}.llvm

# Compile test program
clang -S -emit-llvm -c $src -o ${name}.ll
clang ${name}.ll -o ${name}.out

# Run CPI pass
opt -S -o ${name}.p.ll -load ../build/pass/LLVMCPI.${dll} -cpi -debug-only=cpi ${name}.ll

# Build patched code and link to libsafe_rt
clang ${name}.p.ll -L../build -lsafe_rt -o ${name}.p.out
