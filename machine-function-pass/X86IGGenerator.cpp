// #define DEBUG
#ifdef DEBUG
#define LOG(X) llvm::errs() << X;
#else
#define LOG(X)
#endif

#include "X86.h"
#include "X86InstrInfo.h"        
#include "llvm/CodeGen/MachineFunctionPass.h"   
#include "llvm/CodeGen/MachineInstrBuilder.h"      
// #include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/CodeGen/Spiller.h"
// #include "llvm/CodeGen/RegisterCoalescer.h"
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

std::vector<std::vector<unsigned>> InterferenceGraph; 
std::map<unsigned, unsigned> NumPhysicalRegisters; // type: num of physical registers

class X86IGGenerator : public MachineFunctionPass {
  private:
    bool belongToSameClass(Register reg1, Register reg2);

  public:
    static char ID;
    LiveIntervals *LI;
    MachineFunction *MF;
    const TargetMachine *TM;
    // const TargetRegisterInfo *TRI;
    const MachineLoopInfo *loopInfo;
    const TargetInstrInfo *tii;
    MachineRegisterInfo *mri;
    VirtRegMap *vrm;
    LiveStacks *lss;
    int k;

    X86IGGenerator() : MachineFunctionPass(ID) {
      initializeX86IGGeneratorPass(*PassRegistry::getPassRegistry());
      initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
      initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
      // initializeRegisterCoalescerPass(*PassRegistry::getPassRegistry());
      initializeLiveStacksPass(*PassRegistry::getPassRegistry());
      initializeMachineLoopInfoPass(*PassRegistry::getPassRegistry());
      initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
    }

    StringRef getPassName() const override { 
      return X86_IG_GENERATOR_PASS_NAME; 
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SlotIndexes>();
      AU.addPreserved<SlotIndexes>();
      AU.addRequired<LiveIntervals>();
      // AU.addRequired<RegisterCoalescer>();
      AU.addRequired<LiveStacks>();
      AU.addPreserved<LiveStacks>();
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineLoopInfo>();
      //AU.addRequired<RenderMachineFunction>();
      //if(StrongPHIElim)
      //	AU.addRequiredID(StrongPHIEliminationID);
      AU.addRequired<VirtRegMap>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
    
    bool runOnMachineFunction(MachineFunction &mf) override;
    void printFunction();
    void buildInterferenceGraph();
    void printInterferenceGraph();

};

char X86IGGenerator::ID = 0;

} // end of namespace llvm

//=============================== Private ====================================//

// Check whether two VRs are of the same type
bool X86IGGenerator::belongToSameClass(Register reg1, Register reg2) {
  // LOG("这里1"); LOG("\n");
	const TargetRegisterClass *class1 = MF->getRegInfo().getRegClass(reg1);
  // LOG("这里2"); LOG("\n");
	const TargetRegisterClass *class2 = MF->getRegInfo().getRegClass(reg2);
  // LOG("这里3"); LOG("\n");
  // LOG("\n----------------------\n")
	return class1 == class2;
}


//=============================== Public =====================================//

//Builds Interference Graphs, one for each register type
void X86IGGenerator::buildInterferenceGraph() {
  LOG("Running buildInterferenceGraph()"); LOG("\n");
  LOG("   # of virtual registers: "); LOG(mri->getNumVirtRegs()); LOG("\n");

  std::vector<MachineInstr *> vr_def;
  std::vector<Register> virtual_registers;
	for (unsigned i = 0; i < mri->getNumVirtRegs(); i++) {
		Register ii = Register::index2VirtReg(i);
    if  (mri->getVRegDef(ii) == nullptr || mri->reg_nodbg_empty(ii) || !ii.isVirtual()) {
      continue;
    } else {
#ifdef DEBUG
      mri->getVRegDef(ii)->print(errs());
      errs() << "  :" << mri->getVRegName(ii) << "\n";
#endif
      virtual_registers.push_back(ii);
      vr_def.push_back(mri->getVRegDef(ii));
    }
	}

  // LOG("查看vr\n");
  // for (int i = 0; i < (int)virtual_registers.size(); i++) {
  //   errs() << virtual_registers[i].id() << "\n";
  // }
  // LOG("\n");
  
  InterferenceGraph.resize(virtual_registers.size(), 
                           std::vector<unsigned>(virtual_registers.size()));

  // build interference graph by checking overlapping live interval
  for (unsigned i = 0; i < virtual_registers.size(); i++) {
    Register ii = virtual_registers[i];
    if (LI->hasInterval(ii)) {
			if(ii.isPhysical()) 
				continue;
			const LiveInterval &li = LI->getInterval(ii); 
			// InterferenceGraph[i][i] = 1;  不给自己有interference
			for (unsigned j = i + 1; j < virtual_registers.size(); ++j) {
				Register jj = virtual_registers[j];
        if (LI->hasInterval(jj)) {
					// if(jj.isPhysical() || !belongToSameClass(ii, jj)) {
          if(jj.isPhysical()) {
            // LOG("\n+++++++++++++++++++++++++\n")
						continue;
          }
					const LiveInterval &li2 = LI->getInterval(jj);
          // LOG("++++++++++++++++++++\n")
          // li2.print(errs());
          // LOG("\n++++++++++++++++++++")
					if (li.overlaps(li2)) {
						InterferenceGraph[i][j] = 1;
						InterferenceGraph[j][i] = 1;
#ifdef DEBUG
            LOG("这里有interference:\n");
            vr_def[i]->print(errs());
            vr_def[j]->print(errs());
            LOG("\n");
#endif
					}
				}
			}
		}	
  }

  // print 2d vector for debug
  errs() << "# of virtual reg: " << virtual_registers.size() << "\n";
  errs() << "Interference Graph (adjacency matrix)------------\n";
  for (unsigned int i = 0; i < InterferenceGraph.size(); i++) {
    errs() << "[";
    for (unsigned int j = 0; j < InterferenceGraph[0].size(); j++) {
      errs() << InterferenceGraph[i][j];
      if (j != InterferenceGraph[0].size() - 1) {
        errs() << ", ";
      } else {
        errs() << "]\n";
      }
    }
  }
  errs() << "\n";
}

// Output interference graphs
void X86IGGenerator::printInterferenceGraph() {
  LOG("Running printInterferenceGraph()"); LOG("\n");
	FILE* fp = fopen("interference.csv", "w");
  
  // add # of optimal color used at the beginning
  fprintf(fp, "%u, ", mri->getNumVirtRegs());

  unsigned long long adBits = 0;
	for (unsigned int i = 0; i < InterferenceGraph.size(); i++) {
    unsigned int criteria = std::min(64, (int)InterferenceGraph.size());

    // Write the first LONG
    for (unsigned int j = 0; j < criteria; j++) {
      if (InterferenceGraph[i][j]) {
        unsigned long long one = 1 << (63 - j);
        adBits |= one;
      }
    }
    fprintf(fp, "%llu, ", adBits); 
    
    // Write the second LONG
    adBits = 0;
    for (unsigned int j = criteria; j < InterferenceGraph.size(); j++) {
      if (InterferenceGraph[i][j]) {
        unsigned long long one = 1 << (63 - j);
        adBits |= one;
      }
    }
    fprintf(fp, "%llu, ", adBits); 
  }
	fclose(fp);
}

void X86IGGenerator::printFunction() {
  // Function &F = MF->getFunction();
  // FILE* fp = fopen("../Test/machine_instruction.txt", "w");
  for (MachineBasicBlock &BB : *MF) {
    LOG(BB); LOG("\n");
  }
}

// Run machine function pass
bool X86IGGenerator::runOnMachineFunction(MachineFunction &mf) {
  LOG("\nRunning IGGenerator On function: "); LOG(mf.getFunction().getName()); LOG("\n");
  MF = &mf;
  TM = &MF->getTarget();
	// TRI = TM->getRegisterInfo();
	mri = &MF->getRegInfo(); 
	LI = &getAnalysis<LiveIntervals>();
  LOG("\n++++++++++++++++++++++++++++++++\n");
  // printFunction();
  LOG("++++++++++++++++++++++++++++++++\n");
	buildInterferenceGraph();
	printInterferenceGraph();
	InterferenceGraph.clear( );
	return true;
}

INITIALIZE_PASS_BEGIN(X86IGGenerator, "x86-ig-generator", X86_IG_GENERATOR_PASS_NAME, true, true)
// INITIALIZE_PASS_DEPENDENCY(LiveDebugVariables)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
// INITIALIZE_PASS_DEPENDENCY(RegisterCoalescer)
INITIALIZE_PASS_DEPENDENCY(LiveStacks)
// INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
// INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
// INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_END(X86IGGenerator, "x86-ig-generator", X86_IG_GENERATOR_PASS_NAME, true, true)

FunctionPass *llvm::createX86IGGeneratorPass() { 
  return new X86IGGenerator(); 
}


