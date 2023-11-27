cmake_minimum_required(VERSION 3.20)
find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

message(LLVM_CMAKE_DIR=${LLVM_CMAKE_DIR})
message(llvm_definition=${LLVM_DEFINITIONS})
message(llvm_include_dir=${LLVM_INCLUDE_DIRS})

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
include_directories(${LLVM_INCLUDE_DIRS})

add_subdirectory(IG-generator)