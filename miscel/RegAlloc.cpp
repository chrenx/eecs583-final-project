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
#include <fstream>
#include <sstream>
#include <vector>

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

			map<unsigned, unsigned> pa;
			map<unsigned, unsigned> pb;
			map<unsigned, unsigned> vis;
			map<unsigned, set<unsigned>>AllocGraph;
			unsigned dfn = 0, res = 0;

			map<unsigned, unsigned>ColorResult;
			std::map<unsigned, std::set<unsigned>> VRegAllowedMap;
			map<unsigned, unsigned>UnionFind;
			map<unsigned, set<unsigned>> CongruenceClass;
			set<unsigned> BoundedNodes;
			

			RegAllocGraphColoring() : MachineFunctionPass(ID)
			{
				initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
				initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
				//initializeRegisterCoalescerPass(*PassRegistry::getPassRegistry());
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

			// bi-graph matching
			bool bigraphmatching();
			bool dfs(unsigned v);
			void handle_color_result();
			void preprocess();
			void join(unsigned a, unsigned b);
			bool SameCongruenceClass(unsigned a, unsigned b);
			unsigned find(unsigned x);
			bool UnitOverlap(unsigned a, unsigned b);
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
	set<unsigned> PhysicalRegisters;
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

//Spills virtual register
bool RegAllocGraphColoring::SpillIt(unsigned VReg_index)
{
	// TODO: Check corresponding RenderMachineFunction
	SmallVector<Register, 8> NewIntervals;
	Register VReg = Register::index2VirtReg(VReg_index);
	// FIXME: Do nothing for now
	// VRegsToAlloc.erase(VReg);
	LiveRangeEdit LRE(&(LI->getInterval(VReg)), NewIntervals, *MF, *LI, vrm,
						nullptr, &DeadRemats);
	VRegSpiller->spill(LRE);

	// Copy any newly inserted live intervals into the list of regs to
	// allocate.
	// FIXME: Do nothing for now
	/*
	for (const Register &R : LRE) {
		const LiveInterval &li = LI->getInterval(R);
		assert(!li.empty() && "Empty spill range.");
		//VRegsToAlloc.insert(li.reg());
	}
	*/
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
	unsigned min;
	bool flag = false;
	//find virtual register with minimum degree
	for(map<unsigned, set<unsigned> >::iterator ii = InterferenceGraph.begin(); ii != InterferenceGraph.end(); ii++)
	{
		if(!OnStack[ii->first] && (flag == false || Degree[ii->first] < Degree[min]))
			min = ii->first, flag = true;
	}		
	//if graph empty
	if(flag == false)
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
	TRI = mf.getSubtarget().getRegisterInfo();
	tii = mf.getSubtarget().getInstrInfo();

	vrm = &getAnalysis<VirtRegMap>();
	LI = &getAnalysis<LiveIntervals>();
	lss = &getAnalysis<LiveStacks>();
	MachineBlockFrequencyInfo &MBFI = getAnalysis<MachineBlockFrequencyInfo>();

	VirtRegAuxInfo DefaultVRAI(*MF, *LI, *vrm, getAnalysis<MachineLoopInfo>(),
								MBFI);
	DefaultVRAI.calculateSpillWeightsAndHints();
	VRegSpiller.reset(
		createInlineSpiller(*this, *MF, *vrm, DefaultVRAI));

	bool another_round = false;
	int round = 1;

	errs()<<"Pass before allocation\n";
	//errs()<<*vrm<<"\n";
	//dumpPass();

	preprocess();

	if(!bigraphmatching()){	
	//if(true){
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
			errs( )<<*vrm<<"\n";
		} while(!another_round);
		
		postOptimization();
	}
	errs()<<"Pass after allocation\n";
	errs()<<*vrm<<"\n";

	SlotIndexes& SI = getAnalysis<SlotIndexes>();
	SI.releaseMemory();
	SI.runOnMachineFunction(*MF);

	LiveIntervals& LI = getAnalysis<LiveIntervals>();
	LI.releaseMemory();
	LI.runOnMachineFunction(*MF);
	releaseMemory();

	return true;
}

std::vector<unsigned> Readfile(string filename){
	// Open the file
    std::ifstream file(filename);
    if (!file.is_open()) {
        return {};
    }

    // Read the entire line
    std::string line;
    std::getline(file, line);

    // Close the file
    file.close();

    // Process the values and store them in a vector
    std::vector<unsigned> values;
    std::istringstream iss(line);
    unsigned value;
    char comma;
    
    while (iss >> value) {
        values.push_back(value);
        iss >> comma;  // Read the comma
    }

    // Display the values in the vector (replace this with your logic)
    return values;
}

unsigned RegAllocGraphColoring::find(unsigned x){
	return UnionFind[x]==x ? x : UnionFind[x] = find(UnionFind[x]);
}
bool RegAllocGraphColoring::SameCongruenceClass(unsigned a, unsigned b){
	unsigned fa = find(a), fb = find(b);
	return fa==fb;
}
void RegAllocGraphColoring::join(unsigned a, unsigned b){
	unsigned fa = find(a), fb = find(b);
	UnionFind[fa] = fb;
	return;
}

bool RegAllocGraphColoring::UnitOverlap(unsigned a, unsigned b){
	MCRegister PRegA(a);
	MCRegister PRegB(b);
	for (MCRegUnitIterator Units(PRegA, TRI); Units.isValid(); ++Units) {
		for (MCRegUnitIterator Units2(PRegB, TRI); Units2.isValid(); ++Units2){
			// if two adjacent nodes have common unit, then fail check
			if(*Units == *Units2) return true;
		}
	}
	return false;
}
void RegAllocGraphColoring::preprocess(){
	for (unsigned i = 0, e = mri->getNumVirtRegs();i != e; ++i) {
		Register ii = Register::index2VirtReg(i);
		if (mri->reg_nodbg_empty(ii))
      		continue;
        if (LI->hasInterval(ii)) {
			if(ii.isPhysical()) //  just follow original framework eventhough it seems unnecessary here
				continue;
			unsigned ii_index = ii.virtRegIndex(); 
			const TargetRegisterClass *trc = MF->getRegInfo().getRegClass(ii_index);
			const LiveInterval &li = LI->getInterval(ii);
			VRegAllowedMap[ii_index] = getSetofPotentialRegs(*trc,ii_index);
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
						BoundedNodes.insert(ii_index);
						break;
					}
				}
			}
		}
	}
	for (auto iter: VRegAllowedMap){
		for (auto phyiter: iter.second) UnionFind[phyiter] = phyiter;
	}

	for(auto itera: UnionFind){
		for(auto iterb: UnionFind){
			if(itera!=iterb&&UnitOverlap(itera.first, iterb.first)&&!SameCongruenceClass(itera.first, iterb.first)){
				join(itera.first, iterb.first);
			}
		}
	}
	for(auto itera: UnionFind) find(itera.first);
	for(auto iter:UnionFind){
		CongruenceClass[iter.second].insert(iter.first);
	}
	for(auto iter: VRegAllowedMap){
		set<unsigned>tmp;
		for(auto phyReg: iter.second){
			tmp.insert(UnionFind[phyReg]);
		}
		VRegAllowedMap[iter.first] = tmp;
	}
}

void RegAllocGraphColoring::handle_color_result(){
	// TODO: build AllocGraph that map color to a group of possible phyreg; read result into ColorResult; 
	// AllocGraph: first collect vr that in the same color, then use intersection to narrow done
	auto color_result = Readfile("/home/congyn/eecs583/result.csv");
	auto mapping = Readfile("/home/congyn/eecs583/mapping.csv");
	for(unsigned i = 0, j = 0; i < color_result.size(); j++){
		if(!BoundedNodes.count(mapping[j])) continue;
		ColorResult[mapping[j]] = color_result[i];
		if(!AllocGraph.count(color_result[i])) AllocGraph[color_result[i]] = VRegAllowedMap[mapping[j]];
		else{
			std::set<unsigned> intersection;
			// Use std::set_intersection to find the intersection
			std::set_intersection(
				AllocGraph[color_result[i]].begin(), AllocGraph[color_result[i]].end(),
				VRegAllowedMap[mapping[j]].begin(), VRegAllowedMap[mapping[j]].end(),
				std::inserter(intersection, intersection.begin())
			);
			VRegAllowedMap[color_result[i]] = intersection;
		}	
		i++;
	}
}
// Reference: https://oi-wiki.org/graph/graph-matching/bigraph-match/
bool RegAllocGraphColoring::bigraphmatching(){
	handle_color_result();
	while (true) {
      dfn++;
      unsigned cnt = 0;
      for (auto iter: AllocGraph) {
        if (!pa.count(iter.first) && dfs(iter.first)) {
          cnt++;
        }
      }
      if (cnt == 0) {
        break;
      }
      res += cnt;
    }
    if(res == AllocGraph.size()){
		errs()<<"Happy Christmas!\n";
		for(auto v_reg_color: VRegAllowedMap){
			if(BoundedNodes.count(v_reg_color.first))
			for(auto congruence: CongruenceClass[pa[ColorResult[v_reg_color.first]]]){
				if(compatible_class(*MF,v_reg_color.first,congruence)){
					vrm->assignVirt2Phys(v_reg_color.first, congruence);
					break;
				}
			}
			else{
				if(v_reg_color.second.empty()){
					errs()<<"Cannot assign color to physical register. Spilling needed.";
					return false;
				}
				else for(auto congruence: CongruenceClass[*v_reg_color.second.begin()]){
					if(compatible_class(*MF,v_reg_color.first,congruence)){
						vrm->assignVirt2Phys(v_reg_color.first, congruence);
						break;
					}
				}
			}
		}
		return true;
	}
	else{
		errs()<<"Cannot assign color to physical register. Spilling needed.";
		return false;
	}
	
}
bool RegAllocGraphColoring::dfs(unsigned v) {
    vis[v] = dfn;
    for (unsigned u : AllocGraph[v]) {
      if (!pb.count(u)) {
        pb[u] = v;
        pa[v] = u;
        return true;
      }
    }
    for (unsigned u : AllocGraph[v]) {
      if (vis[pb[u]] != dfn && dfs(pb[u])) {
        pa[v] = u;
        pb[u] = v;
        return true;
      }
    }
    return false;
  }

void RegAllocGraphColoring::releaseMemory() {
  VRegSpiller.reset();
}
FunctionPass *llvm::createColorRegisterAllocator() 
{
	return new RegAllocGraphColoring();
}

