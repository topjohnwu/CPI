# CPI

After cloning the source to a folder `CPI`:

```sh
# Set environment variable for test script
export LLVM_BIN=<path_to_llvm_binaries>
cd CPI

# Always build in separate directory!!
mkdir build
cd build

# Generate makefiles
# You might need to set LLVM_DIR to LLVM sources, or add
# LLVM_BIN to PATH for cmake to work properly
cmake ../

# Run test scripts
cd ../test
./run.sh -b test.c
```

- `test.llvm.ll` is unpatched assembly
- `test.llvm.p.ll` is patched assembly
- `test.llvm.out` is unpatched program
- `test.llvm.p.out` is patched program
