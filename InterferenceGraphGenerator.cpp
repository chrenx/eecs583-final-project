//#include "RenderMachineFunction.h"
//#include "llvm/Function.h"
//#include "VirtRegRewriter.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/CodeGen/Spiller.h"
#include "RegisterCoalescer.h"
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

// references: https://www.kharghoshal.xyz/blog/writing-machinefunctionpass
// https://github.com/sana-damani/LLVM-Optimizations/blob/master/RegAllocGraphColoring.cpp

using namespace llvm;
using namespace std;

#define PASS_NAME "Interference graph generator pass"

namespace {
	vector<vector<unsigned>> InterferenceGraph; 
	map<unsigned, unsigned> NumPhysicalRegisters; // type: num of physical registers

	class InterferenceGraphGenerator : public MachineFunctionPass 
	{
		public:
			static char ID;

			LiveIntervals *LI;
			MachineFunction *MF;
			const TargetMachine *TM;
			const TargetRegisterInfo *TRI;

			MachineRegisterInfo *mri;

			RegAllocGraphColoring() : MachineFunctionPass(ID)
			{
				initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
				initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
				initializeRegisterCoalescerPass(*PassRegistry::getPassRegistry());
				initializeLiveStacksPass(*PassRegistry::getPassRegistry());
				initializeMachineLoopInfoPass(*PassRegistry::getPassRegistry());
				initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
				//initializeRenderMachineFunctionPass(*PassRegistry::getPassRegistry());
				//initializeStrongPHIEliminationPass(*PassRegistry::getPassRegistry());
			}

			virtual const char *getPassName() const
			{
				return PASS_NAME;
			}

			virtual void getAnalysisUsage(AnalysisUsage &AU) const override
			{
				AU.addRequired<SlotIndexes>();
				AU.addPreserved<SlotIndexes>();
				AU.addRequired<LiveIntervals>();
  				//AU.addRequired<RegisterCoalescer>();
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

			bool runOnMachineFunction(MachineFunction &Fn);
			void buildInterferenceGraph();
			void printInterferenceGraph();
			bool same_class(MachineFunction & mf, Register reg1, Register reg2);
			TargetRegisterClass get_reg_class(MachineFunction & mf, Register reg);
			set<unsigned> getNumofPhysicalRegs(TargetRegisterClass trc, unsigned v_reg);
	};

	char InterferenceGraphGenerator::ID = 0;
}

//Builds Interference Graphs, one for each register type
void InterferenceGraphGenerator::buildInterferenceGraph()
{
	InterferenceGraph.resize(mri->getNumVirtRegs(), mri->getNumVirtRegs());

	for (unsigned i = 0; i < mri->getNumVirtRegs(); i++) {
		Register ii = Register::index2VirtReg(i);

        if (LI->hasInterval(ii)) {
			if(ii.isPhysical()) 
				continue;

			const LiveInterval &li = LI->getInterval(ii); 

			InterferenceGraph[i][i] = 1;

			for (unsigned j = i + 1; j < mri->getNumVirtRegs(); ++j) {
				Register jj = Register::index2VirtReg(j);
        		if (LI->hasInterval(jj)) {
					if(jj.isPhysical() || !same_class(ii, jj))
						continue;

					const LiveInterval &li2 = LI->getInterval(jj);
					
					if (li.overlaps(li2)) 
					{
						InterferenceGraph[ii][jj] = 1;
						InterferenceGraph[jj][ii] = 1;
					}
				}
			}
		}	
	}
}

// output interference graphs
void InterferenceGraphGenerator::printInterferenceGraph()
{
	FILE* fp = open("interference.csv", "w");

    unsigned long long adBits = 0;
	for (unsigned int i = 0; i < InterferenceGraph.size(); i++) {

		// Write the first LONG
        for (unsigned int j = 0; j < min(64, InterferenceGraph.size()); j++) {
           	if (InterferenceGraph[i][j]) {
               	unsigned long long one = 1;
                if ( j == 0 )
                    adBits |= one;
                else
                    adBits |= ( (unsigned long long )(one << j ) );
			}
        }
		fprintf(fp, "%llu , ",adBits); 

		// Write the second LONG
		adBits = 0;
    	for ( unsigned int j = min(64,InterferenceGraph.size()); j < InterferenceGraph.size(); j++ ) {
        	if (InterferenceGraph[i][j]) {
            	unsigned long long one = 1;
            	if ( j == 0 )
                	adBits |= one;
            	else
                	adBits |= ( (unsigned long long )(one << j ) );
        	}
    	}
    	fprintf(fp,"%llu , ",adBits); 
    }
	fclose(fp);
}


//This function is used to check whether two VRs are of the same type
bool InterferenceGraphGenerator::same_class(MachineFunction & mf, Register reg1, Register reg2)
{
	TargetRegisterClass class1 = get_reg_class(mf, reg1);
	TargetRegisterClass class2 = get_reg_class(mf, reg2);

	return class1 == class2;
}


TargetRegisterClass InterferenceGraphGenerator::get_reg_class(MachineFunction & mf, Register reg)
{
	TargetRegisterClass *trc = mf.getRegInfo().getRegClass(v_reg);

	return trc;
}


//return the number of physical registers of the same type of a virtual register
unsigned InterferenceGraphGenerator::getNumofPhysicalRegs(TargetRegisterClass trc, unsigned v_reg)
{
	TargetRegisterClass::iterator ii;
	TargetRegisterClass::iterator ee;
	ii = trc.allocation_order_begin(*MF);
	ee = trc.allocation_order_end(*MF);

	while (ii != ee)
	{
		if (trc->contains(*ii)) {
			PhysicalRegisters.insert( *ii );
		}
		ii++;
	}

	return PhysicalRegisters.size();
}


bool InterferenceGraphGenerator::runOnMachineFunction(MachineFunction &mf) 
{
	errs()<<"\nRunning On function: "<< mf.getFunction()->getName();
	MF = &mf;
	TRI = TM->getRegisterInfo();
	mri = &MF->getRegInfo(); 

	LI = &getAnalysis<LiveIntervals>();

	buildInterferenceGraph();
	InterferenceGraphs.clear( );
	NumPhysicalRegisters.clear();

	return true;
}

INITIALIZE_PASS_BEGIN(InterferenceGraphGenerator, "interference-graph-generator", 
	PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(LiveDebugVariables)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(RegisterCoalescer)
INITIALIZE_PASS_DEPENDENCY(LiveStacks)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_END(InterferenceGraphGenerator, "interference-graph-generator", 
	PASS_NAME, false, false)


FunctionPass *llvm::createInterferenceGraphGenerator() 
{
	return new InterferenceGraphGenerator();
}
