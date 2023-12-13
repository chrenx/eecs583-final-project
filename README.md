# eecs583-final-project: group 18
This work contains a interference graph generator pass, a deep learning model, and a registor allocation pass. Two passes were implemented in LLVM source code. That means you have to download LLVM into your local firectory and put two passes there. 

## Important files
- ### demo/entry.py
    - This python file controls the workflow. It runs X86IGGenerator.cpp pass, then deep learning model, and RegAlloc.cpp pass. 
- ### demo/iggenerator.sh
    - It is the script run by entry.py to call X86IGGenerator.cpp pass.
    - -O1 optimization is applied.
- ### demo/regalloc.sh
    - It is the scrip run by entry.py to call RegAlloc.cpp pass.
    - -O1 optimization is applied.
- ### demo/dl_regalloc_model.pth
    - The saved checkpoint
- ### demo/mode.py
    - Architecture of the model.
- ### demo/vr_tracking.csv
    - It keeps track the information of virtural registers corresponding to the interference graph.
- ### machine-function-pass/X86IGGenerator.cpp
    - It is a machine function pass to generate interference graph from C/C++ program.
- ### machine-function-pass/RegAlloc.cpp
    - It is a machine function pass to assign physical registers to virtual registers based on the output from the model.

## Build LLVM
We used LLVM with 16.x version. After installing, put RegAlloc.cpp under "llvm-project/llvm/lib/CodeGen/RegAlloc.cpp", and put X86IGGenerator under "llvm-project/llvm/lib/Target/X86/X86IGGenerator.cpp".   
- For X86IGGenerator pass
    - add ```FunctionPass *createX86IGGeneratorPass();``` in ```X86.h```
    - add ```addPass(createX86IGGeneratorPass());``` under function ```X86PassConfig::addRegAssignAndRewriteOptimized()``` in ```X86TargetMachine.cpp```
    - Put pass name under the CMakeList under ```X86``` folder  
- For RegAlloc pass
    - add ```initializeRegAllocGraphColoringPass(Registry);``` in ```CodeGen.cpp```
    - Put pass name under the CMakeList under ```CodeGen``` folder  


## How to run
- $ conda activate eecs583
- $ pip install -r requirements.txt
- Put the C program under the demo folder
- $ cd demo
- $ python -m entry -f c_program_file_name
