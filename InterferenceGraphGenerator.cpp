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
	map<unsigned, map<unsigned, set<unsigned>>> InterferenceGraphs; // one interference graph for VR of each type
													// type_id: {register_id: {adjacent register_id's}}
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
			void buildInterferenceGraphs();
			void printInterferenceGraphs();
			bool same_class(MachineFunction & mf, unsigned v_reg1, unsigned v_reg2);
			unsigned get_reg_class_id(MachineFunction & mf, unsigned v_reg);
			set<unsigned> getNumofPhysicalRegs(TargetRegisterClass trc, unsigned v_reg);
	};

	char InterferenceGraphGenerator::ID = 0;
}

//Builds Interference Graphs, one for each register type
void InterferenceGraphGenerator::buildInterferenceGraphs()
{
	for (unsigned i = 0; i < mri->getNumVirtRegs(); i++) {
		Register ii = Register::index2VirtReg(i);
        if (LI->hasInterval(ii)) {
			if(ii.isPhysical()) //  just follow original framework eventhough it seems unnecessary here
				continue;
			const LiveInterval &li = LI->getInterval(ii);
			unsigned ii_index = ii.virtRegIndex(); 

			InterferenceGraph[ii_index].insert(0);
			for (unsigned j = 0, e = mri->getNumVirtRegs();j != e; ++j) {
				Register jj = Register::index2VirtReg(j);
        		if (LI->hasInterval(jj)) {
					const LiveInterval &li2 = LI->getInterval(jj);
					unsigned jj_index = jj.virtRegIndex();
					if(jj_index == ii_index)
						continue;
					if(jj.isPhysical()) // same as before
						continue;
					if (li.overlaps(li2)) 
					{
						if(!InterferenceGraph[ii_index].count(jj_index))
						{
							InterferenceGraph[ii_index].insert(jj_index);
							Degree[ii_index]++;
						}
						if(!InterferenceGraph[jj_index].count(ii_index))
						{
							InterferenceGraph[jj_index].insert(ii_index);
							Degree[jj_index]++;
						}
					}
				}
			}
		}	
	}

	for (LiveIntervals::iterator ii = LI->begin(); ii != LI->end(); ii++) 
	{
		
		if(TRI->isPhysicalRegister(ii->first))
			continue;
		   
		const LiveInterval *li = ii->second;

		unsigned class_id = [get_reg_class_id(*MF, ii->first)];

		InterferenceGraphs[class_id][ii->first].insert(ii->first);

		for (LiveIntervals::iterator jj = ii + 1; jj != LI->end(); jj++) 
		{
			const LiveInterval *li2 = jj->second;

			if(TRI->isPhysicalRegister(jj->first) || !same_class(*MF, ii->first, jj->first))
				continue;

			if (li->overlaps(*li2)) {
				InterferenceGraphs[class_id][ii->first].insert(jj->first);
				InterferenceGraphs[class_id][jj->first].insert(ii->first);
			}
		}	
	}
}

// output interference graphs
void InterferenceGraphGenerator::printInterferenceGraphs()
{
	int file_index = 1;
	for (auto it = InterferenceGraphs.begin(); it != InterferenceGraphs.end(); i++) {
		int n = it.second.size();
		vector<vector<int>> adjM(n, n);

		int row_index = 0;
    	for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++) {
			int id1 = it2->first;
			for (int id2: it2->second) {
				adjM[id1][id2] = 1;
			}
    	}

		FILE* fp = open(file_index + ".csv", "w");

        // Write the first LONG
        unsigned long long adBits = 0;
		for (unsigned int i= 0; i < min(64,n); j++) {
        	for (unsigned int j = 0; j < min(64,n); j++) {
           		if (adjM[i][j]) {
               		unsigned long long one = 1;
                	if ( j == 0 )
                    	adBits |= one;
                	else
                    	adBits |= ( (unsigned long long )(one << j ) );
				}
            }
        }
        fprintf(fp, "%llu , ",adBits); 

        
		// Write the second LONG
        adBits = 0;
        for ( unsigned int j = min(64,n); j < n; j++ ) {
            if ( graph_has_edge(g,i,j) ) {
                adjM[i][j]=1;
                unsigned long long one = 1;
                if ( j == 0 )
                    adBits |= one;
                else
                    adBits |= ( (unsigned long long )(one << j ) );
                edges++;
            }
        }
        fprintf(fp,"%llu , ",adBits); 

		fclose(fp);
		index++;
	}
}


//This function is used to check whether two VRs are of the same type
bool InterferenceGraphGenerator::same_class(MachineFunction & mf, unsigned v_reg1, unsigned v_reg2)
{
	unsigned id1 = get_reg_class_id(mf, v_reg1);
	unsigned id2 = get_reg_class_id(mf, v_reg2);

	return id1 == id2;
}


unsigned InterferenceGraphGenerator::get_reg_class_id(MachineFunction & mf, unsigned v_reg)
{
	const TargetRegisterClass *trc = mf.getRegInfo().getRegClass(v_reg);

	return trc->getID();
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
