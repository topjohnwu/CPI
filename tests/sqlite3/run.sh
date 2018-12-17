#!/usr/bin/env bash

[[ -z $LLVM_BIN ]] || export PATH="$LLVM_BIN:$PATH"

if [[ "$1" = "-b" ]]; then
  # Rebuild libraries
  cwd=`pwd`
  cd ../../build
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

clang -emit-llvm -O1 -mllvm -disable-llvm-optzns -c sqlite3.c speedtest1.c

# Original
clang sqlite3.bc speedtest1.bc -o sqlite3.speed.out

# Pass
llvm-link sqlite3.bc speedtest1.bc | opt -alloca-hoisting -mem2reg | opt -o sqlite3.p.bc -load ../../build/pass/LLVMCPI.${dll} -stats $CPI_FLAG
clang sqlite3.p.bc ../../rt.c -o sqlite3.speed.p.out

