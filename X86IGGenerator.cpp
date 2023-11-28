#include "X86.h"
#include "X86InstrInfo.h"        
#include "llvm/CodeGen/MachineFunctionPass.h"   
#include "llvm/CodeGen/MachineInstrBuilder.h"      
#include "llvm/Target/TargetRegisterInfo.h"

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
}

}