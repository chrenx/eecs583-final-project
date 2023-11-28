#include "X86.h"
#include "X86InstrInfo.h"        
#include "llvm/CodeGen/MachineFunctionPass.h"   
#include "llvm/CodeGen/MachineInstrBuilder.h"      
// #include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/CodeGen/Spiller.h"
// #include "RegisterCoalescer.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <memory>
#include <cmath>

using namespace llvm;

#define X86_IG_GENERATOR_PASS_NAME "X86 interference graph generator pass"

namespace {

class X86IGGenerator : public MachineFunctionPass {
    public:
        static char ID;
        LiveIntervals *LI;
        MachineFunction *MF;
        const TargetMachine *TM;
        const TargetRegisterInfo *TRI;
        const MachineLoopInfo *loopInfo;
        const TargetInstrInfo *tii;
        MachineRegisterInfo *mri;
        VirtRegMap *vrm;
        LiveStacks *lss;
        int k;

        X86IGGenerator() : MachineFunctionPass(ID) {
            initializeX86IGGeneratorPass(*PassRegistry::getPassRegistry());
        }

        bool runOnMachineFunction(MachineFunction &mf) override;

        StringRef getPassName() const override { 
            return X86_IG_GENERATOR_PASS_NAME; 
        }

};

char X86IGGenerator::ID = 0;

bool X86IGGenerator::runOnMachineFunction(MachineFunction &mf) {
    errs() << "\n....Running IGGenerator On function: " << mf.getFunction().getName();
    // MF = &mf
	// TRI = TM->getRegisterInfo();
	// mri = &MF->getRegInfo(); 
	// LI = &getAnalysis<LiveIntervals>();
	// buildInterferenceGraph();
	// printInterferenceGraph();
	// InterferenceGraphs.clear( );
	// NumPhysicalRegisters.clear();
	return true;
}

} // end of namespace

INITIALIZE_PASS(X86IGGenerator, "x86-ig-generator", X86_IG_GENERATOR_PASS_NAME,
                true, true)

FunctionPass *llvm::createX86IGGeneratorPass() { 
    return new X86IGGenerator(); 
}

static RegisterPass<X86IGGenerator> X(X86_IG_GENERATOR_PASS_NAME,
      "My Machine Pass", true, true);

