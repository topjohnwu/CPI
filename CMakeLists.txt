cmake_minimum_required(VERSION 3.4.3)
project(CPI)

find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

set(CMAKE_CXX_STANDARD 14)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

if (APPLE)
	list (APPEND CMAKE_CXX_FLAGS "-undefined dynamic_lookup")
endif(APPLE)

add_llvm_loadable_module(
		LLVMCPI
		CPI.cpp
)
