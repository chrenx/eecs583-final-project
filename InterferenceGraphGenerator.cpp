#include "RenderMachineFunction.h"
#include "llvm/Function.h"
#include "VirtRegRewriter.h"
#include "VirtRegMap.h"
#include "Spiller.h"
#include "llvm/CodeGen/RegisterCoalescer.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/LiveStackAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
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
													// type: adjacency matrix
	map<unsigned, unsigned> NumPhysicalRegisters; // type: num of physical registers

	class InterferenceGraphGenerator : public MachineFunctionPass 
	{
		public:
			static char ID;

			LiveIntervals *LI;
			MachineFunction *MF;
			const TargetMachine *TM;
			const TargetRegisterInfo *TRI;

			InterferenceGraphGenerator() : MachineFunctionPass(ID)
			{
				initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
				initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
				initializeRegisterCoalescerAnalysisGroup(*PassRegistry::getPassRegistry());
				initializeLiveStacksPass(*PassRegistry::getPassRegistry());
				initializeMachineLoopInfoPass(*PassRegistry::getPassRegistry());
				initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
				initializeRenderMachineFunctionPass(*PassRegistry::getPassRegistry());
				initializeStrongPHIEliminationPass(*PassRegistry::getPassRegistry());
			}

			virtual const char *getPassName() const
			{
				return PASS_NAME;
			}

			virtual void getAnalysisUsage(AnalysisUsage &AU) const 
			{
				AU.addRequired<SlotIndexes>();
				AU.addPreserved<SlotIndexes>();
				AU.addRequired<LiveIntervals>();
  				AU.addRequired<RegisterCoalescer>();
				AU.addRequired<LiveStacks>();
				AU.addPreserved<LiveStacks>();
				AU.addRequired<MachineLoopInfo>();
				AU.addPreserved<MachineLoopInfo>();
				AU.addRequired<RenderMachineFunction>();
				if(StrongPHIElim)
					AU.addRequiredID(StrongPHIEliminationID);
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
	for (LiveIntervals::iterator ii = LI->begin(); ii != LI->end(); ii++) 
	{
		
		if(TRI->isPhysicalRegister(ii->first))
			continue;
		   
		const LiveInterval *li = ii->second;

		for (LiveIntervals::iterator jj = ii + 1; jj != LI->end(); jj++) 
		{
			const LiveInterval *li2 = jj->second;

			if(TRI->isPhysicalRegister(jj->first) || !same_class(*MF, ii->first, jj->first))
				continue;

			if (li->overlaps(*li2)) 
			{
				unsigned class_id = [get_reg_class_id(*MF, ii->first)];

				if(!InterferenceGraphs[class_id][ii->first].count(jj->first))
				{
					InterferenceGraphs[class_id][ii->first].insert(jj->first);
				}
				if(!InterferenceGraphs[class_id][jj->first].count(ii->first))
				{
					InterferenceGraphs[class_id][jj->first].insert(ii->first);
				}
			}
		}	
	}
}

// TODO
void InterferenceGraphGenerator::printInterferenceGraphs(FILE* fp)
{
    int **adjM;
    adjM = (int **)calloc(n,sizeof(int *));
    for (unsigned int i=0; i < n ; i++) 
         adjM[i] = (int *)calloc(n, sizeof(int));

    for ( unsigned int i = 0; i < n; i++ ) {
        for ( unsigned int j = 0; j < n; j++ ) {
           if ( graph_has_edge(g,i,j) && !graph_has_edge(g,j,i)) {
               graph_add_edge(g,j,i);
           }
        }
    }

    unsigned int edges = 0;
    // adjacency of node i stored in bits
    // so for a 64-bit long we can support a graph
    // up to size 64
    // since we will support up to 128 its two longs
    for ( unsigned int i = 0; i < n; i++ ) {
        //adjM[i][i] = 1;

        // For graph of n > 64 we need 2 LONGs
        // We can support up to 128 nodes
        
        // Write the first LONG
        unsigned long long adBits = 0;
        for ( unsigned int j = 0; j < min(64,n); j++ ) {
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
	TM = &MF->getTarget();
	TRI = TM->getRegisterInfo();

	LI = &getAnalysis<LiveIntervals>();

	buildInterferenceGraph();
	InterferenceGraphs.clear( );
	NumPhysicalRegisters.clear();

	return true;
}

INITIALIZE_PASS(InterferenceGraphGenerator, "interference-graph-generator",
    PASS_NAME,
    true, // is CFG only?
    true  // is analysis?
)


FunctionPass *llvm::createInterferenceGraphGenerator() 
{
	return new InterferenceGraphGenerator();
}
