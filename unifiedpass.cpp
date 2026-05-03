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

// ====================================
// Utils
// ====================================
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



// ====================================
// Functions used in DFA framework
// ====================================
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

  static DenseMap<Value*, BitVector> mergeDenseMaps(std::vector<DenseMap<Value*, BitVector>> maps) {
    DenseMap<Value*, BitVector> newMap;
    for (int i = 0; i < maps.size(); i ++) {
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

  static DenseMap<Value*, BitVector> getInSet(Instruction* Ins, DenseMap<const Instruction*, PointState> st, int size) {
    Instruction* prevIns = Ins->getPrevNode();
    if (prevIns == nullptr) {
        if (Ins->getParent()->isEntryBlock()) {  // if the first instruciton in the function, in set is empty
            DenseMap<Value*, BitVector> emptyMap;
            return emptyMap;
        } else {
            // Ins is the first instruction in a BB. 
            DenseMap<Value*, BitVector> newMap;
            std::vector<DenseMap<Value*, BitVector>> predOutSets;
            for (BasicBlock* pred : predecessors(Ins->getParent())) {
                Instruction* lastIns = &pred->back();
                DenseMap<Value*, BitVector> prevOut = st[lastIns].out;
                predOutSets.push_back(prevOut);
            }   
            return mergeDenseMaps(predOutSets);
        }
    }
    return st[prevIns].out; 
  }

  static std::vector<Instruction*> getSuccessors(Instruction* Ins, Function& F) {
    Instruction* prevIns = Ins->getPrevNode();
    std::vector<Instruction*> succIns;
    if (prevIns == nullptr) {
        if (Ins->getParent() == &F.back()) {
            return succIns;     // return empty bitvector
        } else {
            // collect successor instructions from all succ BBs:
            for (BasicBlock* succ : successors(Ins->getParent())) {
                succIns.push_back(&succ->front());
            }
        }
    } else {
        succIns.push_back(prevIns);
    }
    return succIns;
  }

  static DenseMap<Value*, BitVector> transferFunc(Instruction* Ins, PointState ps, std::vector<Value*> abstractObjects, DominatorTree& DT, Function& F) {
    DenseMap<Value*, BitVector> outMap = ps.in;
    if (auto* Load = dyn_cast<LoadInst>(Ins)) {
        if (Load->getType()->isPointerTy()) {
            // generates a points to relationship between the Value of the load and the load's getPointerOperand
            // union the points to sets of the Value and the pointerOperand

            // handles assignments
            BitVector bv = outMap[Load];
            BitVector ptrBv = outMap[Load->getPointerOperand()];

            std::vector<BitVector> unionParams;
            unionParams.push_back(bv);
            unionParams.push_back(ptrBv);
            BitVector unionSet = meetUnion(unionParams);
            outMap[Load] = unionSet;
        }
            
    }
    // // Stores of a pointer value
    else if (auto* Store = dyn_cast<StoreInst>(Ins)) {
        if (Store->getValueOperand()->getType()->isPointerTy()) {
            // generate a points to relationship between operand(1) -> operand(0)
            // operand 1 holds the target of the store, operand 1 will point to whatever operand 0 points to after this instruction

            Value* aLoc = Store->getValueOperand(); 
            auto it = std::lower_bound(abstractObjects.begin(), abstractObjects.end(), aLoc);

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


            // // kill set:
            BitVector killSet(abstractObjects.size(), false);
            // // strong updates = singleton, nonconditional updates
            BasicBlock* exitBlock = &F.back();
            // // check singletons:
            if (outMap[Store->getPointerOperand()].count() <= 1) {
                // check conditional updates:
                if (DT.dominates(Ins->getParent(), exitBlock)) {
                    killSet = outMap[Store->getPointerOperand()];
                }
            }

            if (killSet.size() != 0) {
                outMap[Store->getPointerOperand()] = bitVectorSub(genSet, killSet);
            } else {
                outMap[Store->getPointerOperand()] = genSet;
            }
            
        }
            
    }
    return outMap;
  }

  PreservedAnalyses run(Function& F, FunctionAnalysisManager& AM) {
    DominatorTree& DT = AM.getResult<DominatorTreeAnalysis>(F);

    // Dataflow objects are maps from pointers to powersets of abstract objects
    // The number of abstract objects is = number of heap alloc calls
    // The number of pointer variables in the program is the number of unique store targets
    DenseMap<Value*, BitVector> universe;
    std::vector<Value*> abstractObjects;
    std::vector<Value*> ptrVariables;
    for (auto& BB : F) {
      for (auto& I : BB) {
        if (auto* Alloca = dyn_cast<AllocaInst>(&I)) {
            if (Alloca->getAllocatedType()->isPointerTy())
                ptrVariables.push_back(Alloca);
        }
        // Heap allocation (call to malloc etc.)
        if (auto* Call = dyn_cast<CallInst>(&I)) {
            if (Call->getType()->isPointerTy())
                abstractObjects.push_back(Call);
        }

        if (auto* Load = dyn_cast<LoadInst>(&I)) {
            if (Load->getType()->isPointerTy())
                ptrVariables.push_back(Load);
        }
        
      }
    }

    Module* M = F.getParent();
    for (auto& G : M->globals()) {
        if (G.getValueType()->isPointerTy()) {
            ptrVariables.push_back(&G);
        } 
    }

    BitVector boundaryCondition(abstractObjects.size(), false);
    BitVector initialValues(abstractObjects.size(), false); 

    for (int i = 0; i < ptrVariables.size(); i ++) {
        universe[ptrVariables[i]] = boundaryCondition;
    }


    // ===============================================
    // ITERATIVE ALG
    // ===============================================
    DenseMap<const Instruction*, PointState> st;

    // // Step 2: Build BFS traversal order starting from entry block
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


    // worklist alg:
    std::vector<Instruction*> worklist;
    worklist = order;
    // worklist.push_back(&(F.getEntryBlock().front()));
    for (int i = 0; i < worklist.size(); i ++) {
        Instruction* Ins = worklist[i];
        // the in set of this instruction is the union of all of the previous instruction's outsets
        PointState ps;
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
