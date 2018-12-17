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
  Linux)
	dll=so
	CPI_FLAG="-cpi"
	;;
  Darwin)
	dll=dylib
	CPI_FLAG="-cpi -debug-only=cpi"
	;;
  *)
	exit 1
	;;
esac

src=$1
name=${src%.*}.llvm

# Compile test program
# clang -S -emit-llvm -c $src -o ${name}.ll

# Compile test program with alloca-hoisting and mem2reg
clang -emit-llvm -O1 -mllvm -disable-llvm-optzns -c $src -o - | opt -S -alloca-hoisting -mem2reg -o ${name}.ll || exit 1
clang -O2 ${name}.ll -o ${name}.out

# Run CPI pass
opt -S -o ${name}.p.ll -load ../build/pass/LLVMCPI.${dll} $CPI_FLAG ${name}.ll || exit 1

# Build patched code
clang -O2 ${name}.p.ll ../rt.c -o ${name}.p.out
exit

# Generate combined bitcode
clang -S -emit-llvm -c ../safe_rt/rt.c -o rt.llvm.ll
llvm-link ${name}.p.ll rt.llvm.ll | opt -S -O2 -o ${name}.o.ll
