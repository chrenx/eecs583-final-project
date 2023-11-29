#define DEBUG_TYPE "regalloc"
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
#include <set>
#include <map>
#include <queue>
#include <memory>
#include <cmath>

using namespace llvm;
using namespace std;

static RegisterRegAlloc
GraphColorRegAlloc("color1", "graph coloring register allocator",
            createColorRegisterAllocator);


namespace {
	map<unsigned, set<unsigned> > InterferenceGraph;
	map<unsigned, int> Degree;
   	map<unsigned, bool> OnStack;
	set<unsigned> Colored;
	BitVector Allocatable;
	set<unsigned> PhysicalRegisters;

	class RegAllocGraphColoring : public MachineFunctionPass 
	{
		public:
			static char ID;

			LiveIntervals *LI;
			MachineFunction *MF;
			const TargetMachine *TM;
			const TargetRegisterInfo *TRI;
  			const MachineLoopInfo *loopInfo;
  			const TargetInstrInfo *tii;
  			MachineRegisterInfo *mri;
			//RenderMachineFunction *rmf;

			VirtRegMap *vrm;
			LiveStacks *lss;
			int k;

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

			//virtual const char *getPassName() const
            StringRef getPassName() const override
			{
				return "Graph Coloring Register Allocator";
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

			bool runOnMachineFunction(MachineFunction &Fn) override;
			void buildInterferenceGraph();
			bool compatible_class(MachineFunction & mf, unsigned v_reg, unsigned p_reg);
			bool aliasCheck(unsigned preg, unsigned vreg);
			set<unsigned> getSetofPotentialRegs(TargetRegisterClass trc,unsigned v_reg);
			unsigned GetReg(set<unsigned> PotentialRegs, unsigned v_reg);
			bool colorNode(unsigned v_reg);
			bool allocateRegisters();
			bool SpillIt(unsigned v_reg);
			void addStackInterval(const LiveInterval*,MachineRegisterInfo *);
			void dumpPass();
	};
	char RegAllocGraphColoring::ID = 0;
}

INITIALIZE_PASS_BEGIN(RegAllocGraphColoring, "regallocbasic", "Test Register Allocator",
                      false, false)
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
INITIALIZE_PASS_END(RegAllocGraphColoring, "regallocbasic", "Test Register Allocator", false,
                    false)

//Builds Interference Graph
void RegAllocGraphColoring::buildInterferenceGraph()
{
	int num=0;
	for (unsigned i = 0, e = mri->getNumVirtRegs();i != e; ++i) {
		Register ii = Register::index2VirtReg(i);
        if (LI->hasInterval(ii)) {
			if(ii.isPhysical()) //  just follow original framework eventhough it seems unnecessary here
				continue;
			const LiveInterval &li = LI->getInterval(ii);
			num++;
			unsigned ii_index = ii.virtRegIndex(); 
			OnStack[ii_index] = false;    
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
	errs( )<<"\nVirtual registers: "<<num;
}

//This function is used to check the compatibility of virtual register with the physical reg.
//For Eg. floating point values must be stored in floating point registers.
bool RegAllocGraphColoring::compatible_class(MachineFunction & mf, unsigned v_reg, unsigned p_reg)
{
	assert(MRegisterInfo::isPhysicalRegister(p_reg) &&
			"Target register must be physical");
	const TargetRegisterClass *trc = mf.getRegInfo().getRegClass(v_reg);

	return trc->contains(p_reg);
}

//check if aliases are empty
bool RegAllocGraphColoring::aliasCheck(unsigned preg, unsigned vreg)
{
	/*
	const unsigned *aliasItr = TRI->getAliasSetForRegister(preg);
	while(*aliasItr != 0)
	{
		for(set<unsigned>::iterator ii = InterferenceGraph[vreg].begin( );
				ii != InterferenceGraph[vreg].end( ); ii++)
		{
			if(Colored.count( *ii ) && vrm->getPhys(*ii) == *aliasItr)
				return false;
		}
		aliasItr++;
	}
	return true;
	*/
	// To be done: figure out how to iter over alias
	return true;
}

//return the set of potential register for a virtual register
set<unsigned> RegAllocGraphColoring::getSetofPotentialRegs(TargetRegisterClass trc,unsigned v_reg)
{
	// TargetRegisterClass::iterator ii;
	// TargetRegisterClass::iterator ee;
	// unsigned p_reg = vrm->getRegAllocPref(v_reg); 
	// This Api is deprecated so just skip for now
	// if(p_reg != 0)
	if(false)
	{
		//PhysicalRegisters.insert( p_reg );
	}
	else
	{
		// Compute an initial allowed set for the current vreg.
		// Inference: RegAllocPBQP.cpp 623::648
		std::vector<MCRegister> VRegAllowed;
		LiveInterval &VRegLI = LI->getInterval(v_reg);
		ArrayRef<MCPhysReg> RawPRegOrder = trc.getRawAllocationOrder(*MF);
		// Record any overlaps with regmask operands.
		BitVector RegMaskOverlaps;
		LI->checkRegMaskInterference(VRegLI, RegMaskOverlaps);
		for (MCPhysReg R : RawPRegOrder) {
			MCRegister PReg(R);
			if (mri->isReserved(PReg))
				continue;

			// vregLI crosses a regmask operand that clobbers preg.
			// if (!RegMaskOverlaps.empty() && !RegMaskOverlaps.test(PReg))
			//	 continue;
			// skip for now

			// vregLI overlaps fixed regunit interference.
			bool Interference = false;
			for (MCRegUnitIterator Units(PReg, TRI); Units.isValid(); ++Units) {
				if (VRegLI.overlaps(LI->getRegUnit(*Units))) {
					Interference = true;
					break;
				}
			}
			if (Interference)
				continue;
			
			// preg is usable for this virtual register.
			PhysicalRegisters.insert(PReg.id());
		}
		/*
		ii = TRI->allocation_order_begin(*MF, trc);
		ee = TRI->allocation_order_end(*MF, trc);
		while(ii != ee)
		{
			PhysicalRegisters.insert( *ii );
			ii++;
		}
		*/
		// TODO: Check if they are same
	}
	k = PhysicalRegisters.size();
	return PhysicalRegisters;
}

//returns the physical register to which the virtual register must be mapped. If there is no
//physical register available this function returns 0.
unsigned RegAllocGraphColoring::GetReg(set<unsigned> PotentialRegs, unsigned v_reg)
{
	for(set<unsigned>::iterator ii = PotentialRegs.begin( ); ii != PotentialRegs.end( ); ii++)
	{
		if(!LI->hasInterval(*ii) || !LI->hasInterval(v_reg))
		{
			continue;
		}
		LiveInterval &li1 = LI->getInterval(*ii);
		LiveInterval &li2 = LI->getInterval(v_reg);
		if( aliasCheck(*ii,v_reg) && !li1.overlaps(li2)&& compatible_class(*MF,v_reg,*ii))
		{
			return *ii;
		}
	}
	return 0;
}

// Adds a stack interval if the given live interval has been
// spilled. Used to support stack slot coloring.

// Temporarily remove
/*
void RegAllocGraphColoring::addStackInterval(const LiveInterval *spilled,
                                    MachineRegisterInfo* mri)
{
	// reg -> reg()
	int stackSlot = vrm->getStackSlot(spilled->reg());

	if (stackSlot == VirtRegMap::NO_STACK_SLOT)
	{
		return;
	}
	// reg -> reg()
	const TargetRegisterClass *RC = mri->getRegClass(spilled->reg());
	LiveInterval &stackInterval = lss->getOrCreateInterval(stackSlot, RC);

	VNInfo *vni;
	if (stackInterval.getNumValNums() != 0)
	{
		vni = stackInterval.getValNumInfo(0);
	} 
	else
 	{
		
		// vni = stackInterval.getNextValue(
		// SlotIndex(), 0, lss->getVNInfoAllocator());
		
		// Not sure if identical: Yuning
		vni = llvm::LiveRange::getNextValue(SlotIndex(), lss->getVNInfoAllocator());
	}

	LiveInterval &rhsInterval = LI->getInterval(spilled->reg());
	stackInterval.MergeRangesInAsValue(rhsInterval, vni);
}
*/
//Spills virtual register
bool RegAllocGraphColoring::SpillIt(unsigned v_reg)
{

	// const LiveInterval* spillInterval = &LI->getInterval(v_reg);
	// SmallVector<LiveInterval*, 8> spillIs;
	// TODE: Check corresponding RenderMachineFunction
	// rmf->rememberUseDefs(spillInterval);
	//std::vector<LiveInterval*> newSpills =
	//	LI->addIntervalsForSpills(*spillInterval, spillIs, loopInfo, *vrm);
	// temporarily remove
	// addStackInterval(spillInterval, mri);
	// rmf->rememberSpills(spillInterval, newSpills);
	// return newSpills.empty();
	return false;
}

//assigns physical to virtual register
bool RegAllocGraphColoring::colorNode(unsigned v_reg)
{
	bool notspilled = true;
	errs()<<"\nColoring Register  : "<<v_reg;
	unsigned p_reg = 0;
	const TargetRegisterClass *trc = MF->getRegInfo().getRegClass(v_reg);
	set<unsigned> PotentialRegs = getSetofPotentialRegs(*trc,v_reg);
	for(set<unsigned>::iterator ii = InterferenceGraph[v_reg].begin( ) ;
			ii != InterferenceGraph[v_reg].end( ); ii++)
	{
		if(Colored.count( *ii ))
			PotentialRegs.erase(vrm->getPhys(*ii));	
		if(PotentialRegs.empty( ))
			break;

	}
	//There are no Potential Physical Registers Available
	if(PotentialRegs.empty( ))
	{
		notspilled = SpillIt(v_reg);
		errs( )<<"\nVreg : "<<v_reg<<" ---> Spilled";
	}
	else
	{
		//Get compatible Physical Register
		p_reg = GetReg(PotentialRegs,v_reg);
		assert(p_reg!=0 && "Assigning 0 reg");
		//if no such register found due to interfernce with p_reg
		if(!p_reg)
		{
			notspilled = SpillIt(v_reg);
			errs( )<<"\nVreg : "<<v_reg<<" ---> Spilled";
		}
		else
		{
			//assigning virtual to physical register
			vrm->assignVirt2Phys( v_reg , p_reg );
			errs( )<<"\nVreg : "<<v_reg<<" ---> Preg :"<<TRI->getName(p_reg);
			Colored.insert(v_reg);
		}
	}
	return notspilled;
}

//This is the main graph coloring algorithm
bool RegAllocGraphColoring::allocateRegisters()
{
	bool round;
	unsigned min = 0;
	//find virtual register with minimum degree
	for(map<unsigned, set<unsigned> >::iterator ii = InterferenceGraph.begin(); ii != InterferenceGraph.end(); ii++)
	{
		if(!OnStack[ii->first] && (min == 0 || Degree[ii->first] < Degree[min]))
			min = ii->first;
	}		
	//if graph empty
	if(min == 0)
		return true;
	errs()<<"\nRegister selected to push on stack = "<<min;

	//push register onto stack
	OnStack[min] = true;

	//delete register from graph
	for(map<unsigned, set<unsigned> >::iterator ii = InterferenceGraph.begin(); ii != InterferenceGraph.end(); ii++)
	{
		if(ii->second.count(min))
			Degree[ii->first]--;
	}

	//recursive call
	round = allocateRegisters();

	//pop and color virtual register
	return colorNode(min) && round;
}


void RegAllocGraphColoring::dumpPass( )
{
	for (MachineFunction::iterator mbbItr = MF->begin(), mbbEnd = MF->end();
			mbbItr != mbbEnd; ++mbbItr) 
	{
		MachineBasicBlock &mbb = *mbbItr;
		errs() << "bb" << mbb.getNumber() << ":\n";
		for (MachineBasicBlock::iterator miItr = mbb.begin(), miEnd = mbb.end();
				miItr != miEnd; ++miItr) 
		{
			MachineInstr &mi = *miItr;
			errs( )<<mi;
		}
	}
}


bool RegAllocGraphColoring::runOnMachineFunction(MachineFunction &mf) 
{
	errs()<<"\nRunning On function: "<<mf.getFunction().getName();
	MF = &mf;
	mri = &MF->getRegInfo(); 
	//&MF->getTarget(); -> &MF->getSubtarget();
	// TM = &MF->getSubtarget();
	TRI = mf.getSubtarget().getRegisterInfo();
	tii = mf.getSubtarget().getInstrInfo();

	vrm = &getAnalysis<VirtRegMap>();
	LI = &getAnalysis<LiveIntervals>();
	lss = &getAnalysis<LiveStacks>();
	//rmf = &getAnalysis<RenderMachineFunction>();
	loopInfo = &getAnalysis<MachineLoopInfo>();

	bool another_round = false;
	int round = 1;

	errs()<<"Pass before allocation\n";
	dumpPass();

	do
	{
		errs( )<<"\nRound #"<<round<<'\n';
		round++;
		vrm->clearAllVirt();
		buildInterferenceGraph();
		another_round = allocateRegisters();
		InterferenceGraph.clear( );
		Degree.clear( );
		OnStack.clear( );
		Colored.clear();
		Allocatable.clear();
		PhysicalRegisters.clear();
		errs( )<<*vrm;
	} while(!another_round);

	//rmf->renderMachineFunction( "After GraphColoring Register Allocator" , vrm );

	//std::unique_ptr<VirtRegRewriter> rewriter(createVirtRegRewriter());

	//this is used to write the final code.
	//rewriter->runOnMachineFunction(*MF, *vrm, LI);

	errs()<<"Pass after allocation\n";
	dumpPass();

	vrm->dump( );

	return true;
}


FunctionPass *llvm::createColorRegisterAllocator() 
{
	return new RegAllocGraphColoring();
}
/*
//===-- RegAllocBasic.cpp - Basic Register Allocator ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RAMy function pass, which provides a minimal
// implementation of the basic register allocator.
//
//===----------------------------------------------------------------------===//

#include "AllocationOrder.h"
#include "LiveDebugVariables.h"
#include "RegAllocBase.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/Spiller.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

static RegisterRegAlloc MyRegAlloc("myreg", "my register allocator",
                                      createMyRegisterAllocator);

namespace {
  struct CompSpillWeight {
    bool operator()(const LiveInterval *A, const LiveInterval *B) const {
      return A->weight() < B->weight();
    }
  };
}

namespace {
/// RAMy provides a minimal implementation of the basic register allocation
/// algorithm. It prioritizes live virtual registers by spill weight and spills
/// whenever a register is unavailable. This is not practical in production but
/// provides a useful baseline both for measuring other allocators and comparing
/// the speed of the basic algorithm against other styles of allocators.
class RAMy : public MachineFunctionPass,
                public RegAllocBase,
                private LiveRangeEdit::Delegate {
  // context
  MachineFunction *MF;

  // state
  std::unique_ptr<Spiller> SpillerInstance;
  std::priority_queue<const LiveInterval *, std::vector<const LiveInterval *>,
                      CompSpillWeight>
      Queue;

  // Scratch space.  Allocated here to avoid repeated malloc calls in
  // selectOrSplit().
  BitVector UsableRegs;

  bool LRE_CanEraseVirtReg(Register) override;
  void LRE_WillShrinkVirtReg(Register) override;

public:
  RAMy(const RegClassFilterFunc F = allocateAllRegClasses);

  /// Return the pass name.
  StringRef getPassName() const override { return "My Register Allocator"; }

  /// RAMy analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void releaseMemory() override;

  Spiller &spiller() override { return *SpillerInstance; }

  void enqueueImpl(const LiveInterval *LI) override { Queue.push(LI); }

  const LiveInterval *dequeue() override {
    if (Queue.empty())
      return nullptr;
    const LiveInterval *LI = Queue.top();
    Queue.pop();
    return LI;
  }

  MCRegister selectOrSplit(const LiveInterval &VirtReg,
                           SmallVectorImpl<Register> &SplitVRegs) override;

  /// Perform register allocation.
  bool runOnMachineFunction(MachineFunction &mf) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().set(
      MachineFunctionProperties::Property::IsSSA);
  }

  // Helper for spilling all live virtual registers currently unified under preg
  // that interfere with the most recently queried lvr.  Return true if spilling
  // was successful, and append any new spilled/split intervals to splitLVRs.
  bool spillInterferences(const LiveInterval &VirtReg, MCRegister PhysReg,
                          SmallVectorImpl<Register> &SplitVRegs);

  static char ID;
};

char RAMy::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(RAMy, "regallocmy", "My Register Allocator",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(LiveDebugVariables)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(RegisterCoalescer)
INITIALIZE_PASS_DEPENDENCY(MachineScheduler)
INITIALIZE_PASS_DEPENDENCY(LiveStacks)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_END(RAMy, "regallocmy", "My Register Allocator", false,
                    false)

bool RAMy::LRE_CanEraseVirtReg(Register VirtReg) {
  LiveInterval &LI = LIS->getInterval(VirtReg);
  if (VRM->hasPhys(VirtReg)) {
    Matrix->unassign(LI);
    aboutToRemoveInterval(LI);
    return true;
  }
  // Unassigned virtreg is probably in the priority queue.
  // RegAllocBase will erase it after dequeueing.
  // Nonetheless, clear the live-range so that the debug
  // dump will show the right state for that VirtReg.
  LI.clear();
  return false;
}

void RAMy::LRE_WillShrinkVirtReg(Register VirtReg) {
  if (!VRM->hasPhys(VirtReg))
    return;

  // Register is assigned, put it back on the queue for reassignment.
  LiveInterval &LI = LIS->getInterval(VirtReg);
  Matrix->unassign(LI);
  enqueue(&LI);
}

RAMy::RAMy(RegClassFilterFunc F):
  MachineFunctionPass(ID),
  RegAllocBase(F) {
}

void RAMy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addRequired<LiveIntervals>();
  AU.addPreserved<LiveIntervals>();
  AU.addPreserved<SlotIndexes>();
  AU.addRequired<LiveDebugVariables>();
  AU.addPreserved<LiveDebugVariables>();
  AU.addRequired<LiveStacks>();
  AU.addPreserved<LiveStacks>();
  AU.addRequired<MachineBlockFrequencyInfo>();
  AU.addPreserved<MachineBlockFrequencyInfo>();
  AU.addRequiredID(MachineDominatorsID);
  AU.addPreservedID(MachineDominatorsID);
  AU.addRequired<MachineLoopInfo>();
  AU.addPreserved<MachineLoopInfo>();
  AU.addRequired<VirtRegMap>();
  AU.addPreserved<VirtRegMap>();
  AU.addRequired<LiveRegMatrix>();
  AU.addPreserved<LiveRegMatrix>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void RAMy::releaseMemory() {
  SpillerInstance.reset();
}


// Spill or split all live virtual registers currently unified under PhysReg
// that interfere with VirtReg. The newly spilled or split live intervals are
// returned by appending them to SplitVRegs.
bool RAMy::spillInterferences(const LiveInterval &VirtReg,
                                 MCRegister PhysReg,
                                 SmallVectorImpl<Register> &SplitVRegs) {
  // Record each interference and determine if all are spillable before mutating
  // either the union or live intervals.
  SmallVector<const LiveInterval *, 8> Intfs;

  // Collect interferences assigned to any alias of the physical register.
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, *Units);
    for (const auto *Intf : reverse(Q.interferingVRegs())) {
      if (!Intf->isSpillable() || Intf->weight() > VirtReg.weight())
        return false;
      Intfs.push_back(Intf);
    }
  }
  LLVM_DEBUG(dbgs() << "spilling " << printReg(PhysReg, TRI)
                    << " interferences with " << VirtReg << "\n");
  assert(!Intfs.empty() && "expected interference");

  // Spill each interfering vreg allocated to PhysReg or an alias.
  for (unsigned i = 0, e = Intfs.size(); i != e; ++i) {
    const LiveInterval &Spill = *Intfs[i];

    // Skip duplicates.
    if (!VRM->hasPhys(Spill.reg()))
      continue;

    // Deallocate the interfering vreg by removing it from the union.
    // A LiveInterval instance may not be in a union during modification!
    Matrix->unassign(Spill);

    // Spill the extracted interval.
    LiveRangeEdit LRE(&Spill, SplitVRegs, *MF, *LIS, VRM, this, &DeadRemats);
    spiller().spill(LRE);
  }
  return true;
}

// Driver for the register assignment and splitting heuristics.
// Manages iteration over the LiveIntervalUnions.
//
// This is a minimal implementation of register assignment and splitting that
// spills whenever we run out of registers.
//
// selectOrSplit can only be called once per live virtual register. We then do a
// single interference test for each register the correct class until we find an
// available register. So, the number of interference tests in the worst case is
// |vregs| * |machineregs|. And since the number of interference tests is
// minimal, there is no value in caching them outside the scope of
// selectOrSplit().
MCRegister RAMy::selectOrSplit(const LiveInterval &VirtReg,
                                  SmallVectorImpl<Register> &SplitVRegs) {
  // Populate a list of physical register spill candidates.
  SmallVector<MCRegister, 8> PhysRegSpillCands;

  // Check for an available register in this class.
  auto Order =
      AllocationOrder::create(VirtReg.reg(), *VRM, RegClassInfo, Matrix);
  for (MCRegister PhysReg : Order) {
    assert(PhysReg.isValid());
    // Check for interference in PhysReg
    switch (Matrix->checkInterference(VirtReg, PhysReg)) {
    case LiveRegMatrix::IK_Free:
      // PhysReg is available, allocate it.
      return PhysReg;

    case LiveRegMatrix::IK_VirtReg:
      // Only virtual registers in the way, we may be able to spill them.
      PhysRegSpillCands.push_back(PhysReg);
      continue;

    default:
      // RegMask or RegUnit interference.
      continue;
    }
  }

  // Try to spill another interfering reg with less spill weight.
  for (MCRegister &PhysReg : PhysRegSpillCands) {
    if (!spillInterferences(VirtReg, PhysReg, SplitVRegs))
      continue;

    assert(!Matrix->checkInterference(VirtReg, PhysReg) &&
           "Interference after spill.");
    // Tell the caller to allocate to this newly freed physical register.
    return PhysReg;
  }

  // No other spill candidates were found, so spill the current VirtReg.
  LLVM_DEBUG(dbgs() << "spilling: " << VirtReg << '\n');
  if (!VirtReg.isSpillable())
    return ~0u;
  LiveRangeEdit LRE(&VirtReg, SplitVRegs, *MF, *LIS, VRM, this, &DeadRemats);
  spiller().spill(LRE);

  // The live virtual register requesting allocation was spilled, so tell
  // the caller not to allocate anything during this round.
  return 0;
}

bool RAMy::runOnMachineFunction(MachineFunction &mf) {
  LLVM_DEBUG(dbgs() << "********** BASIC REGISTER ALLOCATION **********\n"
                    << "********** Function: " << mf.getName() << '\n');

  MF = &mf;
  RegAllocBase::init(getAnalysis<VirtRegMap>(),
                     getAnalysis<LiveIntervals>(),
                     getAnalysis<LiveRegMatrix>());
  VirtRegAuxInfo VRAI(*MF, *LIS, *VRM, getAnalysis<MachineLoopInfo>(),
                      getAnalysis<MachineBlockFrequencyInfo>());
  VRAI.calculateSpillWeightsAndHints();

  SpillerInstance.reset(createInlineSpiller(*this, *MF, *VRM, VRAI));

  allocatePhysRegs();
  postOptimization();

  // Diagnostic output before rewriting
  LLVM_DEBUG(dbgs() << "Post alloc VirtRegMap:\n" << *VRM << "\n");

  releaseMemory();
  return true;
}

FunctionPass* llvm::createMyRegisterAllocator() {
  return new RAMy();
}
*/