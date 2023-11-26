//#include "RenderMachineFunction.h"
//#include "llvm/Function.h"
//#include "VirtRegRewriter.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/CodeGen/Spiller.h"
#include "RegisterCoalescer.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <set>
#include <map>
#include <queue>
#include <memory>
#include <cmath>

#define DEBUG_TYPE "regalloc"

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
  			//const MachineLoopInfo *loopInfo;
  			const TargetInstrInfo *tii;
  			MachineRegisterInfo *mri;
			std::unique_ptr<Spiller> VRegSpiller;
			//  RenderMachineFunction *rmf;
			/// Inst which is a def of an original reg and whose defs are already all
			/// dead after remat is saved in DeadRemats. The deletion of such inst is
			/// postponed till all the allocations are done, so its remat expr is
			/// always available for the remat of all the siblings of the original reg.
			SmallPtrSet<MachineInstr *, 32> DeadRemats;

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
				AU.setPreservesCFG();
				AU.addRequired<AAResultsWrapperPass>();
				AU.addPreserved<AAResultsWrapperPass>();
				AU.addRequired<SlotIndexes>();
				AU.addPreserved<SlotIndexes>();
				AU.addRequired<LiveIntervals>();
				AU.addPreserved<LiveIntervals>();
  				//AU.addRequired<RegisterCoalescer>();
				AU.addRequired<LiveStacks>();
				AU.addPreserved<LiveStacks>();
				AU.addRequired<MachineBlockFrequencyInfo>();
  				AU.addPreserved<MachineBlockFrequencyInfo>();
				AU.addRequired<MachineLoopInfo>();
				AU.addPreserved<MachineLoopInfo>();
				//AU.addRequired<RenderMachineFunction>();
				//if(StrongPHIElim)
				//	AU.addRequiredID(StrongPHIEliminationID);
				AU.addRequired<MachineDominatorTree>();
  				AU.addPreserved<MachineDominatorTree>();
				AU.addRequired<VirtRegMap>();
				AU.addPreserved<VirtRegMap>();
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
			void postOptimization();
			void releaseMemory() override;
	};
	char RegAllocGraphColoring::ID = 0;
}

INITIALIZE_PASS(RegAllocGraphColoring, "regallocbasic", "Test Register Allocator",
                      false, false)
/*
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
*/
//Builds Interference Graph
void RegAllocGraphColoring::buildInterferenceGraph()
{
	int num=0;
	errs()<<"Number of VirRegs is "<<mri->getNumVirtRegs()<<"\n";
	for (unsigned i = 0, e = mri->getNumVirtRegs();i != e; ++i) {
		Register ii = Register::index2VirtReg(i);
		if (mri->reg_nodbg_empty(ii))
      		continue;
        if (LI->hasInterval(ii)) {
			if(ii.isPhysical()) //  just follow original framework eventhough it seems unnecessary here
				continue;
			const LiveInterval &li = LI->getInterval(ii);
			num++;
			unsigned ii_index = ii.virtRegIndex(); 
			OnStack[ii_index] = false;    
			//FIXME: What's the meaning of this line of code? Confusing
			InterferenceGraph[ii_index].insert(-1);
			for (unsigned j = 0, e = mri->getNumVirtRegs();j != e; ++j) {
				Register jj = Register::index2VirtReg(j);
				if (mri->reg_nodbg_empty(jj))
      				continue;
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
	//FIXME: Checkout correctness for iter over alias
	
	MCRegister PReg(preg);
	LiveInterval &VRegLI = LI->getInterval(vreg);

	// vregLI overlaps fixed regunit interference.
	for (MCRegUnitIterator Units(PReg, TRI); Units.isValid(); ++Units) {
		// make sure not interfere with original phy reg. maybe unnecessary
		if (VRegLI.overlaps(LI->getRegUnit(*Units))) {
			return false;
		}
		for(set<unsigned>::iterator ii = InterferenceGraph[vreg].begin();
				ii != InterferenceGraph[vreg].end(); ii++){
			if(Colored.count( *ii )){
				MCRegister temp_PReg = vrm->getPhys(*ii);
				for (MCRegUnitIterator Units2(temp_PReg, TRI); Units2.isValid(); ++Units2){
					// if two adjacent nodes have common unit, then fail check
					if(*Units == *Units2) return false;
				}
			}
		}
	}
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
			// FIXME: Figure out meaning of this
			if (!RegMaskOverlaps.empty() && !RegMaskOverlaps.test(PReg))
				continue;

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
		// if(!LI->hasInterval(*ii) || !LI->hasInterval(v_reg))
		// FIXME: It seems that now hasInterval only checks virtual register
		if(!LI->hasInterval(v_reg))
		{
			continue;
		}
		// LiveInterval &li1 = LI->getInterval(*ii);
		// LiveInterval &li2 = LI->getInterval(v_reg);
		// if( aliasCheck(*ii,v_reg) && !li1.overlaps(li2)&& compatible_class(*MF,v_reg,*ii))
		// FIXME: Try to mix aliascheck with overlap since the basic logic are similar. Check for correctness.
		if( aliasCheck(*ii,v_reg) && compatible_class(*MF,v_reg,*ii))
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
bool RegAllocGraphColoring::SpillIt(unsigned VReg_index)
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
	SmallVector<Register, 8> NewIntervals;
	Register VReg = Register::index2VirtReg(VReg_index);
	// FIXME: Do nothing for now
	// VRegsToAlloc.erase(VReg);
	LiveRangeEdit LRE(&(LI->getInterval(VReg)), NewIntervals, *MF, *LI, vrm,
						nullptr, &DeadRemats);
	VRegSpiller->spill(LRE);

	/*
	LLVM_DEBUG(dbgs() << "VREG " << printReg(VReg, &TRI) << " -> SPILLED (Cost: "
						<< LRE.getParent().weight() << ", New vregs: ");
	*/
	// Copy any newly inserted live intervals into the list of regs to
	// allocate.
	for (const Register &R : LRE) {
		const LiveInterval &li = LI->getInterval(R);
		assert(!li.empty() && "Empty spill range.");
		//LLVM_DEBUG(dbgs() << printReg(li.reg(), &tri) << " ");
		// FIXME: Do nothing for now
		//VRegsToAlloc.insert(li.reg());
	}

	//LLVM_DEBUG(dbgs() << ")\n");
	return NewIntervals.empty();
}

//assigns physical to virtual register
bool RegAllocGraphColoring::colorNode(unsigned v_reg)
{
	bool notspilled = true;
	errs()<<"\nColoring Register  : "<<v_reg;
	unsigned p_reg = 0;
	const TargetRegisterClass *trc = MF->getRegInfo().getRegClass(v_reg);
	set<unsigned> PotentialRegs = getSetofPotentialRegs(*trc,v_reg);
	errs()<<"\nPotential register count is "<<PotentialRegs.size();
	for(set<unsigned>::iterator ii = InterferenceGraph[v_reg].begin( ) ;
			ii != InterferenceGraph[v_reg].end( ); ii++)
	{
		if(Colored.count( *ii )){
			PotentialRegs.erase(vrm->getPhys(*ii));	
			errs()<<"\nInterfere with %"<<*ii;
		}
		if(PotentialRegs.empty( ))
			break;

	}
	//There are no Potential Physical Registers Available
	if(PotentialRegs.empty( ))
	{
		errs()<<"Empty potentialRegs\n";
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
			errs( )<<"\nVreg : "<<v_reg<<" ---> Spilled\n";
		}
		else
		{
			//assigning virtual to physical register
			vrm->assignVirt2Phys( v_reg , p_reg );
			errs( )<<"\nVreg : "<<v_reg<<" ---> Preg :"<<TRI->getName(p_reg)<<"\n";
			Colored.insert(v_reg);
		}
	}
	return notspilled;
}

//This is the main graph coloring algorithm
bool RegAllocGraphColoring::allocateRegisters()
{
	bool round;
	unsigned min = -1;
	//find virtual register with minimum degree
	for(map<unsigned, set<unsigned> >::iterator ii = InterferenceGraph.begin(); ii != InterferenceGraph.end(); ii++)
	{
		if(!OnStack[ii->first] && (min == -1 || Degree[ii->first] < Degree[min]))
			min = ii->first;
	}		
	//if graph empty
	if(min == -1)
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

void RegAllocGraphColoring::postOptimization() {
  VRegSpiller->postOptimization();
  /// Remove dead defs because of rematerialization.
  for (auto *DeadInst : DeadRemats) {
    LI->RemoveMachineInstrFromMaps(*DeadInst);
    DeadInst->eraseFromParent();
  }
  DeadRemats.clear();
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
	MachineBlockFrequencyInfo &MBFI = getAnalysis<MachineBlockFrequencyInfo>();
	//rmf = &getAnalysis<RenderMachineFunction>();
	//loopInfo = &getAnalysis<MachineLoopInfo>();

	// FIXME: we create DefaultVRAI here to match existing behavior pre-passing
	// the VRAI through the spiller to the live range editor. However, it probably
	// makes more sense to pass the PBQP VRAI. The existing behavior had
	// LiveRangeEdit make its own VirtRegAuxInfo object.

	VirtRegAuxInfo DefaultVRAI(*MF, *LI, *vrm, getAnalysis<MachineLoopInfo>(),
								MBFI);
	DefaultVRAI.calculateSpillWeightsAndHints();
	VRegSpiller.reset(
		createInlineSpiller(*this, *MF, *vrm, DefaultVRAI));
	std::unique_ptr<Spiller> VRegSpiller(
      createInlineSpiller(*this, *MF, *vrm, DefaultVRAI));

	bool another_round = false;
	int round = 1;

	errs()<<"Pass before allocation\n";
	errs()<<*vrm<<"\n";
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
		errs( )<<*vrm<<"\n";
	} while(!another_round);
	
	//rmf->renderMachineFunction( "After GraphColoring Register Allocator" , vrm );

	//std::unique_ptr<VirtRegRewriter> rewriter(createVirtRegRewriter());

	//this is used to write the final code.
	//rewriter->runOnMachineFunction(*MF, *vrm, LI);
	postOptimization();

	errs()<<"Pass after allocation\n";
	errs()<<*vrm<<"\n";
	dumpPass();

	//vrm->dump( );
	SlotIndexes& SI = getAnalysis<SlotIndexes>();
	SI.releaseMemory();
	SI.runOnMachineFunction(*MF);

	LiveIntervals& LI = getAnalysis<LiveIntervals>();
	LI.releaseMemory();
	LI.runOnMachineFunction(*MF);
	releaseMemory();

	return true;
}

void RegAllocGraphColoring::releaseMemory() {
  VRegSpiller.reset();
}
FunctionPass *llvm::createColorRegisterAllocator() 
{
	return new RegAllocGraphColoring();
}
