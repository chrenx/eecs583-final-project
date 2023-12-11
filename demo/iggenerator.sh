#!/bin/bash
# Run script for Homework 2 EECS 583 Fall 2023
# Place this script in the benchmarks folder and run it using the name of the file (without the file type)
# e.g. sh run.sh hw2correct1


# Delete outputs from previous runs. Update this when you want to retain some files.
rm -f default.profraw *_prof *_fplicm *.bc *.profdata *_output *.ll *.s

# Convert source code to bitcode (IR).
clang ${1}.c -S -O2 -emit-llvm -o ${1}.ll
../llvm-project/build/bin/llc ${1}.ll -march=x86-64 -o ${1}.s
clang ${1}.s -o ${1} -no-pie
./${1}

rm -f default.profraw *_prof *_fplicm *.bc *.profdata *_output *.ll *.s

# /home/xxx/Desktop/eecs583/final-project/llvm-project/build/bin/llc ${1}.ll -debug-pass=Structure -march=x86-64 -o ${1}.s

# clang ${1}.c -O0 -S -emit-llvm -disable-O0-optnone -o ${1}.ll
# clang ${1}.ll -O2 -Wall -o ${1}
# llc ${1}.ll -march=x86-64 -o ${1}.s
# clang ${1}.s -o ${1}

# clang -fprofile-instr-generate ${1}.ls.bc -o ${1}

# llc ${1}.bc -march=x86-64 -filetype=obj -o ${1}.o

# clang -fprofile-instr-generate ${1}.o -o ${1}





# # Canonicalize natural loops (Ref: llvm.org/doxygen/LoopSimplify_8h_source.html)
# opt -passes='loop-simplify' ${1}.bc -o ${1}.ls.bc

# # Instrument profiler passes.
# opt -passes='pgo-instr-gen,instrprof' ${1}.ls.bc -o ${1}.ls.prof.bc

# # Note: We are using the New Pass Manager for these passes! 

# # Generate binary executable with profiler embedded
# clang -fprofile-instr-generate ${1}.ls.prof.bc -o ${1}_prof

# # When we run the profiler embedded executable, it generates a default.profraw file that contains the profile data.
# ./${1}_prof > correct_output

# # Converting it to LLVM form. This step can also be used to combine multiple profraw files,
# # in case you want to include different profile runs together.
# llvm-profdata merge -o ${1}.profdata default.profraw

# # The "Profile Guided Optimization Use" pass attaches the profile data to the bc file.
# opt -passes="pgo-instr-use" -o ${1}.profdata.bc -pgo-test-profile-file=${1}.profdata < ${1}.ls.prof.bc > /dev/null

# # We now use the profile augmented bc file as input to your pass.
# opt -load-pass-plugin="${PATH2LIB}" -passes="${PASS}" ${1}.profdata.bc -o ${1}.fplicm.bc > /dev/null

# # Generate binary excutable before FPLICM: Unoptimzed code
# clang ${1}.ls.bc -o ${1}_no_fplicm 
# # Generate binary executable after FPLICM: Optimized code
# clang ${1}.fplicm.bc -o ${1}_fplicm

# # Produce output from binary to check correctness
# ./${1}_fplicm > fplicm_output

# echo -e "\n=== Program Correctness Validation ==="
# if [ "$(diff correct_output fplicm_output)" != "" ]; then
#     echo -e ">> Outputs do not match\n"
# else
#     echo -e ">> Outputs match\n"
#     # Measure performance
#     echo -e "1. Performance of unoptimized code"
#     time ./${1}_no_fplicm > /dev/null
#     echo -e "\n\n"
#     echo -e "2. Performance of optimized code"
#     time ./${1}_fplicm > /dev/null
#     echo -e "\n\n"
# fi

# Cleanup: Remove this if you want to retain the created files. And you do need to.

