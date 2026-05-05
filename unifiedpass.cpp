// =============================================================================
// ECE/CS 5544 Group Project - Flow-Sensitive Per program point points-to analysis
// 
// This project is an implementation of the flow-sensitive per-program-point
// points-to analysis. It implements a data flow analysis framework
// where data flow values are maps from pointers to powersets of abstract objects.
// 
// Authors: Yiyun Huang and Megan Farran
// =============================================================================


#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/IR/Dominators.h"


using namespace llvm;

namespace {

// =============================================================================
// Utils
// =============================================================================
struct PointState {
    DenseMap<Value*, BitVector> in;
    DenseMap<Value*, BitVector> out;
    DenseMap<Value*, BitVector> gen;   
    DenseMap<Value*, BitVector> kill; 
  };

void prettyPrintPointState(raw_ostream& OS, PointState ps, std::vector<Value*> abstractObjects) {
    for (auto& [key, value] : ps.out) {
        if (key->getName() == "") continue;
        OS << "  " << key->getName() << ": { ";
        bool first = true;
        for (unsigned i = 0; i < value.size(); ++i) {
            if (!value.test(i)) continue;
            if (!first) OS << "; ";
            first = false;
            OS << abstractObjects[i]->getName();
        }
        OS << " }\n";
    }
}

// Meet operator: intersect 
static BitVector meetIntersect(const std::vector<BitVector>& ins) {
  if (ins.empty()) return {};
  BitVector out = ins[0];
  for (size_t i = 1; i < ins.size(); ++i) out &= ins[i];
  return out;
}

// Meet operator: union 
static BitVector meetUnion(const std::vector<BitVector>& ins) {
  if (ins.empty()) return {};
  BitVector out = ins[0];
  for (size_t i = 1; i < ins.size(); ++i) out |= ins[i];
  return out;
}

// Set difference: bv1 - bv2 (clears bits in bv1 that are also set in bv2)
static BitVector bitVectorSub(BitVector bv1, BitVector bv2) {
  if (bv1.size() != bv2.size()) return {};
  for (int i = 0; i < bv1.size(); ++i) {
    if (bv1[i] == 1 && bv2[i] == 1) {
      bv1.flip(static_cast<unsigned>(i));
    }
  }
  return bv1;
}


struct PointsTo : PassInfoMixin<PointsTo> {

    // A helper function to merge multiple DenseMap<Value*, BitVector> objects. 
    static DenseMap<Value*, BitVector> mergeDenseMaps(std::vector<DenseMap<Value*, BitVector>> maps) {
        DenseMap<Value*, BitVector> newMap;
        for (int i = 0; i < maps.size(); i ++) {    // loop through all of the maps

            // for each map, if the key exists in newMap, merge the values. 
            // if the key does not exist in newMap, add the key, value pair 
            for (auto& [key, value] : maps[i]) {
                auto it = newMap.find(key);
                if (it != newMap.end()) {
                    // merge values
                    auto& newMapValue = it->second;
                    std::vector<BitVector> unionParams;
                    unionParams.push_back(newMapValue);
                    unionParams.push_back(value);
                    newMap[key] = meetUnion(unionParams);
                } else {
                    // add k,v pair to new map
                    newMap.insert({key, value});
                }
            }
        }
        return newMap;
    }

    // Helper function to get the in set from an Instruction.
    // The in set for an instruction is the out set of the previous instruction
    // 
    // If the instruction is the first instruction in the program,
    // there is no previous instruction, so return an empty map.   
    // 
    // If the instruction is the first instruction in a basic block,
    // take the last instruction from each predecessor of the basic block. 
    static DenseMap<Value*, BitVector> getInSet(Instruction* Ins, DenseMap<const Instruction*, PointState> st, int size) {
        Instruction* prevIns = Ins->getPrevNode();
        if (prevIns == nullptr) {
            if (Ins->getParent()->isEntryBlock()) {  // if the first instruciton in the program, in set is empty
                DenseMap<Value*, BitVector> emptyMap;
                return emptyMap;
            } else {
                // Ins is the first instruction in a BB. 
                DenseMap<Value*, BitVector> newMap;
                std::vector<DenseMap<Value*, BitVector>> predOutSets;

                // for each predecessor, grab the last instruction in the predecessor
                for (BasicBlock* pred : predecessors(Ins->getParent())) {
                    Instruction* lastIns = &pred->back();
                    DenseMap<Value*, BitVector> prevOut = st[lastIns].out;
                    predOutSets.push_back(prevOut);
                }   
                // merge the predecessors outsets (this is akin to the union meet operator in typical data flow analysis)
                return mergeDenseMaps(predOutSets);
            }
        }
        return st[prevIns].out;     // return the out set from the previous instruction
    }

    // Helper function to get the successors of an instruction
    // The successor of an instruction is the next instruction after Ins
    // 
    // If the next instruction after Ins is the last instruciton in the function, 
    // Ins has no successors
    // 
    // If Ins is the last instruction in a basic block, collect the first instruction
    // from each successor of the current basic block
    static std::vector<Instruction*> getSuccessors(Instruction* Ins, Function& F) {
        Instruction* nextIns = Ins->getNextNode();
        std::vector<Instruction*> succIns;
        if (nextIns == nullptr) {
            if (Ins->getParent() == &F.back()) {
                return succIns;     // return empty bitvector
            } else {
                // collect successor instructions from all succ BBs:
                for (BasicBlock* succ : successors(Ins->getParent())) {
                    succIns.push_back(&succ->front());
                }
            }
        } else {
            succIns.push_back(nextIns);     // just return next instruction after Ins
        }
        return succIns;
    }

    // given a value, return the set of aLocs that that value points to
    static std::vector<Value*> getPointsToValue(DenseMap<Value*, BitVector> pointsToInfo, Value* pointer, std::vector<Value*> abstractObjects) {
        BitVector bv = pointsToInfo[pointer];
        std::vector<Value*> rtn;

        for (unsigned i = 0; i < bv.size(); ++i) {
            if (!bv.test(i)) continue;
            rtn.push_back(abstractObjects[i]);
        }
        return rtn;
    }   

  static DenseMap<Value*, BitVector> transferFunc(Instruction* Ins, PointState ps, std::vector<Value*> abstractObjects, DominatorTree& DT, Function& F) {
    DenseMap<Value*, BitVector> outMap = ps.in;
    if (auto* Load = dyn_cast<LoadInst>(Ins)) {
        if (Load->getType()->isPointerTy()) {
            // generates a points to relationship between the Value of the load and the load's getPointerOperand
            // union the points to sets of the Value and the pointerOperand

            Value* pointerOperand = Load->getPointerOperand();
            auto it2 = std::find(abstractObjects.begin(), abstractObjects.end(), pointerOperand);

            // if loading from a non abstract object
            if (it2 == abstractObjects.end() && *it2 != pointerOperand) {
                std::vector<Value*> pointerTargets = getPointsToValue(outMap, pointerOperand, abstractObjects);
                if (pointerTargets.size() == 1) {  //make a test case for this
                    pointerOperand = pointerTargets[0];
                }
            }

            // handles assignments
            BitVector bv = outMap[Load];
            BitVector ptrBv = outMap[pointerOperand];

            std::vector<BitVector> unionParams;
            unionParams.push_back(bv);
            unionParams.push_back(ptrBv);
            BitVector unionSet = meetUnion(unionParams);
            outMap[Load] = unionSet;
        }
            
    }
    // Stores of a pointer value
    else if (auto* Store = dyn_cast<StoreInst>(Ins)) {
        if (Store->getValueOperand()->getType()->isPointerTy()) {
            // generate a points to relationship between getPointerOperand -> getValueOperand
            outs() << "store ins: " << *Store << "\n";

            Value* aLoc = Store->getValueOperand(); 
            auto it = std::find(abstractObjects.begin(), abstractObjects.end(), aLoc);
            Value* pointerOperand = Store->getPointerOperand();
            auto it2 = std::find(abstractObjects.begin(), abstractObjects.end(), pointerOperand);

            // handles instructions in the form *c = a;
            // if the store target is not an abstract location, need to find the abstract location that 
            // the store target points to
            if (it2 == abstractObjects.end() && *it2 != pointerOperand) {
                if (auto* Load = dyn_cast<LoadInst>(pointerOperand)) {
                    if (Load->getType()->isPointerTy()) {
                        // need to get the value that c points to in order to assign it
                        std::vector<Value*> pointerTargets = getPointsToValue(outMap, Load, abstractObjects);
                        if (pointerTargets.size() == 1) {  //make a test case for this
                            pointerOperand = pointerTargets[0];
                        }
                    }
                }
            }
          
            // gen set:
            BitVector genSet(abstractObjects.size(), false);  
            if (it != abstractObjects.end() && *it == aLoc) {
                genSet.set(static_cast<unsigned>(it - abstractObjects.begin()));
            } else {
                // handles assignments
                BitVector bv = outMap[Store->getValueOperand()];
                BitVector ptrBv = outMap[Store->getPointerOperand()];

                std::vector<BitVector> unionParams;
                unionParams.push_back(bv);
                unionParams.push_back(ptrBv);
                BitVector unionSet = meetUnion(unionParams);
                genSet = unionSet;
            }


            // kill set:
            BitVector killSet(abstractObjects.size(), false);
            // strong updates = singleton, nonconditional updates
            BasicBlock* exitBlock = &F.back();
            // check singletons:
            if (outMap[Store->getPointerOperand()].count() <= 1) {
                // check conditional updates:
                if (DT.dominates(Ins->getParent(), exitBlock)) {
                    killSet = outMap[Store->getPointerOperand()];
                }
            }

            if (killSet.size() != 0) {
                outMap[pointerOperand] = bitVectorSub(genSet, killSet);
            } else {
                outMap[pointerOperand] = genSet;
            }
            
        }
            
    }
    return outMap;
  }

  PreservedAnalyses run(Function& F, FunctionAnalysisManager& AM) {
    DominatorTree& DT = AM.getResult<DominatorTreeAnalysis>(F);

    // Dataflow objects are maps from pointers to powersets of abstract objects
    // The types of abstract objects are defined in the Anderson paper
    std::vector<Value*> abstractObjects;
    for (auto& BB : F) {
      for (auto& I : BB) {
        // stack allocations
        if (auto* Alloca = dyn_cast<AllocaInst>(&I)) {
            abstractObjects.push_back(Alloca); 
        }
        // Heap allocation (ex: call to malloc)
        if (auto* Call = dyn_cast<CallInst>(&I)) {
            if (Call->getType()->isPointerTy()) 
                abstractObjects.push_back(Call);
        }
      }
    }

    // Include global variables as abstract locations (as per Anderson's paper)
    // Reasoning: pointers can point to other pointers
    Module* M = F.getParent();
    for (auto& G : M->globals()) {
        if (G.getValueType()->isPointerTy()) {
            abstractObjects.push_back(&G);
        } 
    }

    // outs() << "abstract objects: \n";
    // for (int i = 0; i < abstractObjects.size(); i ++) {
    //     outs() << *(abstractObjects[i]) << "\n";
    // }


    // =============================================================================
    // ITERATIVE ALG
    // =============================================================================
    DenseMap<const Instruction*, PointState> st;

    // Build BFS traversal order starting from entry block
    std::vector<Instruction*> order;
    std::vector<BasicBlock*> bbs;
    bbs.push_back(&F.getEntryBlock());
    for (auto& I : *bbs[0]) {       // instructions from first block
        order.push_back(&I);
    }
    for (size_t i = 0; i < bbs.size(); ++i) {
      for (BasicBlock* succ : successors(bbs[i])) {
        if (std::find(bbs.begin(), bbs.end(), succ) == bbs.end()) {
            bbs.push_back(succ);
            for (auto& I : *succ) {     // all other instructions
                order.push_back(&I);
            }
        } 
      }
    }

    // -----------------------------------------------------------------------------
    // WORKLIST ALGORITHM
    // 
    // The worklist initially contains all instructions in the program. 
    // Upon each iteration, compare to see if the out set of the current instruction 
    // has changed. If is has, add all of the instruction's successors to the worklist.
    // If the instruction's out set has not changed, continue iterating through the 
    // worklist. 
    // 
    // Upon each iteration, calculate the in set of an instruction. 
    //      IN[Ins] = union of OUT[pred] for all predecessor instructions
    // Then, utilize the transfer funciton to find the new out set. The transfer
    // function handles the generating, killing, and propogation of points-to 
    // relationships. 
    // -----------------------------------------------------------------------------
    std::vector<Instruction*> worklist;
    worklist = order;
    for (int i = 0; i < worklist.size(); i ++) {
        Instruction* Ins = worklist[i];
        PointState ps = st[Ins];

        ps.in = getInSet(Ins, st, abstractObjects.size());
        
        DenseMap<Value*, BitVector> newOut = transferFunc(Ins, ps, abstractObjects, DT, F);

        if (ps.out != newOut) {    
            ps.out = newOut;
            std::vector<Instruction*> succInstructions = getSuccessors(Ins, F);
            for (int j = 0; j < succInstructions.size(); j ++) {
                worklist.push_back(succInstructions[j]);
            }
        }
        st[Ins] = ps;
    }


    // printing results:
    outs() << "Per-program point point-to sets: \n";
    for (int j = 0; j < order.size(); j ++) {
        outs() << "Program Point: "<< *order[j] << " \n";
        prettyPrintPointState(outs(), st[order[j]], abstractObjects);
    }
    return PreservedAnalyses::all();
  }
};

} // end namespace 


// =============================================================================
// Pass Registration: registers all four passes as a single LLVM plugin
// =============================================================================
// Usage: opt -load-pass-plugin=unifiedpass.so -passes='<passname>' input.bc
// Where <passname> is: flow-sensitive-points-to-analysis
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "UnifiedPass", "v1.0", [](PassBuilder& PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager& FPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                  if (Name == "flow-sensitive-points-to-analysis") {
                    FPM.addPass(PointsTo());
                    return true;
                  }
                  return false;
                });
          }};
}
