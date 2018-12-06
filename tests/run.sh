#!/usr/bin/env bash

# Usage: ./run.sh <source_file>
# Note: llvm built binaries can be in LLVM_BIN variable

[ -z $LLVM_BIN ] || export PATH="$LLVM_BIN:$PATH"

if [ "$1" = "-b" ]; then
  # Rebuild
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

# Get filename
src=$1
name=${src%.*}.llvm

# Compile program
clang -emit-llvm -c $src -o ${name}.bc
llvm-dis ${name}.bc -o ${name}.ll
clang ${name}.bc -o ${name}.out

# Run our pass
opt -load ../build/LLVMCPI.${dll} -cpi ${name}.bc > ${name}.p.bc
llvm-dis ${name}.p.bc -o ${name}.p.ll
clang ${name}.p.bc -o ${name}.p.out

# Clean all bytecode (we can't read it anyways..)
rm -f ${name}*.bc
