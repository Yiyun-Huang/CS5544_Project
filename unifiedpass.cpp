// =============================================================================
// ECE/CS 5544 Group Project - Points-to analyses (flow-sensitive + baseline)
//
// This file implements two LLVM passes that share infrastructure for
// abstract-object collection and metric reporting:
//
//   1. flow-sensitive-points-to-analysis (FlowSensitivePointsTo)
//      Per-program-point points-to maps, embedded in a worklist dataflow
//      framework. Strong-vs-weak update is decided per store using a
//      dominator-based heuristic.
//
//   2. flow-insensitive-points-to-analysis (FlowInsensitivePointsTo)
//      Classic Andersen-style inclusion-constraint analysis:
//      one points-to map per function, all stores are weak updates,
//      iterated to a fixed point. This is our primary baseline.
//
// Both passes report the same evaluation metrics so the two can be compared
// directly on the same input.
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

#include <chrono>
#include <set>


using namespace llvm;

namespace {

// =============================================================================
// Shared utilities
// =============================================================================
struct PointState {
    DenseMap<Value*, BitVector> in;     // may in
    DenseMap<Value*, BitVector> out;    // may out
    DenseMap<Value*, BitVector> mustin;
    DenseMap<Value*, BitVector> mustout;    
    DenseMap<Value*, BitVector> gen;    // cosnt gen
    DenseMap<Value*, BitVector> depgen;
    DenseMap<Value*, BitVector> kill;   // const kill
    DenseMap<Value*, BitVector> depkill;
};

void prettyPrintPointState(raw_ostream& OS, DenseMap<Value*, BitVector> dfaValue, std::vector<Value*> abstractObjects) {
    if (dfaValue.empty()) OS << "  { }\n";
    for (auto& [key, value] : dfaValue) {
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

void prettyPrintMap(raw_ostream& OS, const DenseMap<Value*, BitVector>& m,
                    const std::vector<Value*>& abstractObjects) {
    for (auto& [key, value] : m) {
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

static BitVector meetIntersect(const std::vector<BitVector>& ins) {
    if (ins.empty()) return {};
    BitVector out = ins[0];
    for (size_t i = 1; i < ins.size(); ++i) out &= ins[i];
    return out;
}

static BitVector meetUnion(const std::vector<BitVector>& ins) {
    if (ins.empty()) return {};
    BitVector out = ins[0];
    for (size_t i = 1; i < ins.size(); ++i) out |= ins[i];
    return out;
}

// Set difference: bv1 - bv2 (clears bits in bv1 that are also set in bv2)
static BitVector bitVectorSub(BitVector bv1, BitVector bv2) {
  if (bv1.size() != bv2.size()) {
    if (bv2.size() == 0) {
        return bv1;
    } else if (bv1.size() == 0) {
        return bv2;
    } else return {};
  }
  for (int i = 0; i < bv1.size(); ++i) {
    if (bv1[i] == 1 && bv2[i] == 1) {
      bv1.flip(static_cast<unsigned>(i));
    }
  }
  return bv1;
}

// Collect abstract objects: stack allocas, malloc-like calls returning a
// pointer, and module-level pointer-typed globals. Both passes use this.
static std::vector<Value*> collectAbstractObjects(Function& F) {
    std::vector<Value*> abstractObjects;
    for (auto& BB : F) {
        for (auto& I : BB) {
            if (auto* Alloca = dyn_cast<AllocaInst>(&I)) {
                abstractObjects.push_back(Alloca);
            }
            if (auto* Call = dyn_cast<CallInst>(&I)) {
                if (Call->getType()->isPointerTy())
                    abstractObjects.push_back(Call);
            }
        }
    }
    Module* M = F.getParent();
    for (auto& G : M->globals()) {
        if (G.getValueType()->isPointerTy()) {
            abstractObjects.push_back(&G);
        }
    }
    return abstractObjects;
}

// Decode a BitVector into the list of abstract locations it represents.
static std::vector<Value*> getPointsToValueFromBV(BitVector bv, std::vector<Value*> abstractObjects) {
    std::vector<Value*> rtn;
    for (unsigned i = 0; i < bv.size(); ++i) {
        if (!bv.test(i)) continue;
        rtn.push_back(abstractObjects[i]);
    }
    return rtn;
}

// =============================================================================
// Metric reporting: shared across both passes so the same shape of output
// shows up no matter which pass is producing it.
// =============================================================================

// Compute and print metrics from a per-program-point map (flow-sensitive case).
static void reportMetricsPerPoint(raw_ostream& OS,
                                  Function& F,
                                  const std::vector<Instruction*>& order,
                                  const DenseMap<const Instruction*, PointState>& st,
                                  std::size_t abstractObjCount,
                                  std::size_t worklistIters,
                                  double analysisTimeMs,
                                  StringRef passLabel) {
    double avgSize = 0.0;
    unsigned numPointers = 0;
    if (!order.empty()) {
        const Instruction* exit = order.back();
        auto exitIt = st.find(exit);
        if (exitIt != st.end()) {
            const auto& exitOut = exitIt->second.out;
            unsigned totalBits = 0;
            for (const auto& kv : exitOut) {
                if (kv.first->getName().empty()) continue;
                numPointers++;
                totalBits += kv.second.count();
            }
            if (numPointers > 0) {
                avgSize = static_cast<double>(totalBits) / static_cast<double>(numPointers);
            }
        }
    }

    std::set<std::pair<Value*, Value*>> mayPairs;
    std::set<std::pair<Value*, Value*>> mustPairs;

    for (const Instruction* I : order) {
        auto it = st.find(I);
        if (it == st.end()) continue;
        const auto& m = it->second.out;

        std::vector<std::pair<Value*, BitVector>> ptrs;
        ptrs.reserve(m.size());
        for (const auto& kv : m) {
            if (kv.first->getName().empty()) continue;
            if (kv.second.count() == 0) continue;
            ptrs.push_back({kv.first, kv.second});
        }

        for (size_t i = 0; i < ptrs.size(); ++i) {
            for (size_t j = i + 1; j < ptrs.size(); ++j) {
                Value* a = ptrs[i].first;
                Value* b = ptrs[j].first;
                Value* lo = a < b ? a : b;
                Value* hi = a < b ? b : a;

                BitVector inter = ptrs[i].second;
                inter &= ptrs[j].second;
                if (inter.count() == 0) continue;

                mayPairs.insert({lo, hi});

                if (ptrs[i].second.count() == 1
                    && ptrs[j].second.count() == 1
                    && ptrs[i].second == ptrs[j].second) {
                    mustPairs.insert({lo, hi});
                }
            }
        }
    }

    OS << "\n=== Evaluation Metrics [" << passLabel << "] for function '" << F.getName() << "' ===\n";
    OS << "  pointer variables tracked at exit : " << numPointers << "\n";
    OS << "  avg points-to set size at exit    : " << format("%.3f", avgSize) << "\n";
    OS << "  #may-alias pairs   (any point)    : " << mayPairs.size() << "\n";
    OS << "  #must-alias pairs  (any point)    : " << mustPairs.size() << "\n";
    OS << "  analysis time                     : " << format("%.3f ms", analysisTimeMs) << "\n";
    OS << "  program points analyzed           : " << order.size() << "\n";
    OS << "  abstract objects                  : " << abstractObjCount << "\n";
    OS << "  worklist iterations               : " << worklistIters << "\n";
    OS << "============================================\n";
}

// Compute and print metrics from a single (flow-insensitive) map.
static void reportMetricsSingle(raw_ostream& OS,
                                Function& F,
                                const DenseMap<Value*, BitVector>& m,
                                std::size_t abstractObjCount,
                                std::size_t numProgramPoints,
                                std::size_t iterations,
                                double analysisTimeMs,
                                StringRef passLabel) {
    double avgSize = 0.0;
    unsigned numPointers = 0;
    unsigned totalBits = 0;
    for (const auto& kv : m) {
        if (kv.first->getName().empty()) continue;
        numPointers++;
        totalBits += kv.second.count();
    }
    if (numPointers > 0) {
        avgSize = static_cast<double>(totalBits) / static_cast<double>(numPointers);
    }

    std::set<std::pair<Value*, Value*>> mayPairs;
    std::set<std::pair<Value*, Value*>> mustPairs;

    std::vector<std::pair<Value*, BitVector>> ptrs;
    ptrs.reserve(m.size());
    for (const auto& kv : m) {
        if (kv.first->getName().empty()) continue;
        if (kv.second.count() == 0) continue;
        ptrs.push_back({kv.first, kv.second});
    }
    for (size_t i = 0; i < ptrs.size(); ++i) {
        for (size_t j = i + 1; j < ptrs.size(); ++j) {
            Value* a = ptrs[i].first;
            Value* b = ptrs[j].first;
            Value* lo = a < b ? a : b;
            Value* hi = a < b ? b : a;

            BitVector inter = ptrs[i].second;
            inter &= ptrs[j].second;
            if (inter.count() == 0) continue;

            mayPairs.insert({lo, hi});

            if (ptrs[i].second.count() == 1
                && ptrs[j].second.count() == 1
                && ptrs[i].second == ptrs[j].second) {
                mustPairs.insert({lo, hi});
            }
        }
    }

    OS << "\n=== Evaluation Metrics [" << passLabel << "] for function '" << F.getName() << "' ===\n";
    OS << "  pointer variables tracked          : " << numPointers << "\n";
    OS << "  avg points-to set size             : " << format("%.3f", avgSize) << "\n";
    OS << "  #may-alias pairs                   : " << mayPairs.size() << "\n";
    OS << "  #must-alias pairs                  : " << mustPairs.size() << "\n";
    OS << "  analysis time                      : " << format("%.3f ms", analysisTimeMs) << "\n";
    OS << "  program points analyzed            : " << numProgramPoints << "\n";
    OS << "  abstract objects                   : " << abstractObjCount << "\n";
    OS << "  fixpoint iterations                : " << iterations << "\n";
    OS << "============================================\n";
}


// =============================================================================
// Pass 1: flow-sensitive points-to analysis
// =============================================================================
struct FlowSensitivePointsTo : PassInfoMixin<FlowSensitivePointsTo> {

    // A helper function to merge multiple DenseMap<Value*, BitVector> objects. 
    static DenseMap<Value*, BitVector> denseMapSub(DenseMap<Value*, BitVector> dm1, DenseMap<Value*, BitVector> dm2) {
        DenseMap<Value*, BitVector> newMap = dm1; 
        for (auto& [key, value] : newMap) {
            auto it = dm2.find(key);
            if (it != dm2.end()) {  // if the key exists in dm2
                value = bitVectorSub(value, it->second);
            } 
        }
        return newMap;
    }

    static DenseMap<Value*, BitVector> mergeDenseMaps(std::vector<DenseMap<Value*, BitVector>> maps) {
        DenseMap<Value*, BitVector> newMap;
        for (int i = 0; i < (int)maps.size(); i++) {
            for (auto& [key, value] : maps[i]) {
                auto it = newMap.find(key);
                if (it != newMap.end()) {
                    auto& newMapValue = it->second;

                    BitVector unionCalc = newMapValue;
                    unionCalc |= value;
                    newMap[key] = unionCalc;
                } else {
                    newMap.insert({key, value});
                }   
            }
        }
        return newMap;
    }

    static DenseMap<Value*, BitVector> intersectDenseMaps(std::vector<DenseMap<Value*, BitVector>> maps) {
        DenseMap<Value*, BitVector> newMap = maps[0];
        for (int i = 1; i < (int)maps.size(); i++) { 
            DenseMap<Value*, BitVector> tempMap;
            for (auto& [key, value] : maps[i]) {
                auto it = newMap.find(key);
                if (it == newMap.end()) continue;  // key not in newMap
                auto& newMapValue = it->second;
                std::vector<BitVector> intersectParams;
                intersectParams.push_back(newMapValue);
                intersectParams.push_back(value);
                tempMap[key] = meetIntersect(intersectParams);
            }
            newMap = tempMap;
        }
        return newMap;
    }

    static DenseMap<Value*, BitVector> getInSet(Instruction* Ins, DenseMap<const Instruction*, PointState> st, bool isMustIn) {
        Instruction* prevIns = Ins->getPrevNode();
        if (prevIns == nullptr) {
            if (Ins->getParent()->isEntryBlock()) {
                DenseMap<Value*, BitVector> emptyMap;
                return emptyMap;
            } else {
                std::vector<DenseMap<Value*, BitVector>> predOutSets;
                for (BasicBlock* pred : predecessors(Ins->getParent())) {
                    Instruction* lastIns = &pred->back();
                    if (isMustIn) {
                        predOutSets.push_back(st[lastIns].mustout);
                    } else {
                        predOutSets.push_back(st[lastIns].out);
                    }
                }
                if (isMustIn) {
                    return intersectDenseMaps(predOutSets);
                } else {
                    return mergeDenseMaps(predOutSets);
                }
                
            }
        }
        if (isMustIn) {
            return st[prevIns].mustout;
        } 
        return st[prevIns].out;
    }

    static std::vector<Instruction*> getSuccessors(Instruction* Ins, Function& F) {
        Instruction* nextIns = Ins->getNextNode();
        std::vector<Instruction*> succIns;
        if (nextIns == nullptr) {
            if (Ins->getParent() == &F.back()) {
                return succIns;
            } else {
                for (BasicBlock* succ : successors(Ins->getParent())) {
                    succIns.push_back(&succ->front());
                }
            }
        } else {
            succIns.push_back(nextIns);
        }
        return succIns;
    }

    // Transfer function: OUT[Ins] from IN[Ins].
    static PointState transferFunc(Instruction* Ins, PointState ps, DenseMap<Value*, BitVector> prevIn, std::vector<Value*> abstractObjects) {
        BitVector constGen(abstractObjects.size(), false); 
        BitVector depGen(abstractObjects.size(), false); 
        BitVector constKill(abstractObjects.size(), true); 
        // BitVector depKill(abstractObjects.size(), false); 
        if (auto* Load = dyn_cast<LoadInst>(Ins)) {
            if (Load->getType()->isPointerTy()) {
                Value* pointerOperand = Load->getPointerOperand();
                auto it2 = std::find(abstractObjects.begin(), abstractObjects.end(), pointerOperand);
                bool pointerOperandIsAloc = it2 != abstractObjects.end() && *it2 == pointerOperand;

                if (pointerOperandIsAloc) {
                    ps.gen[Load] = prevIn[pointerOperand];
                } else {
                    BitVector bv = prevIn[pointerOperand]; 
                    std::vector<Value*> targets = getPointsToValueFromBV(bv, abstractObjects);
                    for (int i = 0; i < targets.size(); i ++) {
                        ps.depgen[Load] |= prevIn[targets[i]];
                    }
                }
            }
        }
        // Stores of a pointer value
        else if (auto* Store = dyn_cast<StoreInst>(Ins)) {
            if (Store->getValueOperand()->getType()->isPointerTy()) {
                Value* targetValue = Store->getValueOperand(); 
                auto it = std::find(abstractObjects.begin(), abstractObjects.end(), targetValue);
                bool targetIsAloc = it != abstractObjects.end() && *it == targetValue;

                Value* pointerOperand = Store->getPointerOperand();
                auto it2 = std::find(abstractObjects.begin(), abstractObjects.end(), pointerOperand);
                bool pointerOperandIsAloc = it2 != abstractObjects.end() && *it2 == pointerOperand;

                // CONSTGEN (stores in the form x = where x is a pointer var (not a pointer indirection))

                if (pointerOperandIsAloc) {
                    if (targetIsAloc) {
                        // a = &x
                        constGen.set(static_cast<unsigned>(it - abstractObjects.begin()));
                    } else {
                        // a = b
                        constGen = prevIn[targetValue];  // pointer will point to everything that the targetValue currently points to
                    }

                    ps.gen[pointerOperand] = constGen;
                    ps.kill[pointerOperand] = constKill;
                    
                } else {
                    // DEPGEN
                    BitVector bv = prevIn[pointerOperand]; 
                    std::vector<Value*> targets = getPointsToValueFromBV(bv, abstractObjects);
                    if (targetIsAloc) {
                        for (int i = 0; i < targets.size(); i ++) {
                            depGen.set(static_cast<unsigned>(it - abstractObjects.begin()));
                            ps.depgen[targets[i]] = depGen;
                        }
                    } else {
                        for (int i = 0; i < targets.size(); i ++) {
                            ps.depgen[targets[i]] = prevIn[targetValue]; 
                            ps.depkill[targets[i]] = prevIn[targets[i]];
                        }    
                    }
                }
            }
        }
        
        return ps;
    }


    PreservedAnalyses run(Function& F, FunctionAnalysisManager& AM) {
        std::vector<Value*> abstractObjects = collectAbstractObjects(F);
        // outs() << "abstract objects \n";
        // for (int i = 0; i < abstractObjects.size(); i ++) {
        //     outs() << *abstractObjects[i] << "\n";
        // }

        DenseMap<const Instruction*, PointState> st;
        std::vector<Instruction*> order;
        std::vector<BasicBlock*> bbs;
        bbs.push_back(&F.getEntryBlock());
        for (auto& I : *bbs[0]) {
            order.push_back(&I);
        }
        for (size_t i = 0; i < bbs.size(); ++i) {
            for (BasicBlock* succ : successors(bbs[i])) {
                if (std::find(bbs.begin(), bbs.end(), succ) == bbs.end()) {
                    bbs.push_back(succ);
                    for (auto& I : *succ) {
                        order.push_back(&I);
                    }
                }
            }
        }

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<Instruction*> worklist;
        worklist = order;
        for (int i = 0; i < (int)worklist.size(); i++) {
            Instruction* Ins = worklist[i];
            PointState ps = st[Ins];
            ps.mustin = getInSet(Ins, st, true);
            ps.in = getInSet(Ins, st, false);

            PointState newMayPs = transferFunc(Ins, ps, ps.in, abstractObjects);
            PointState newMustPs = transferFunc(Ins, ps, ps.mustin, abstractObjects);

            std::vector<DenseMap<Value*, BitVector>> unionParams;
            unionParams.push_back(newMayPs.kill);
            unionParams.push_back(newMayPs.depkill);
            DenseMap<Value*, BitVector> killsetMay = mergeDenseMaps(unionParams);

            unionParams.clear();
            unionParams.push_back(newMustPs.kill);
            unionParams.push_back(newMustPs.depkill);
            DenseMap<Value*, BitVector> killsetMust = mergeDenseMaps(unionParams);

            unionParams.clear();
            unionParams.push_back(newMayPs.gen);
            unionParams.push_back(newMayPs.depgen);
            DenseMap<Value*, BitVector> gensetMay = mergeDenseMaps(unionParams);

            unionParams.clear();
            unionParams.push_back(newMustPs.gen);
            unionParams.push_back(newMustPs.depgen);
            DenseMap<Value*, BitVector> gensetMust = mergeDenseMaps(unionParams);

            // kill = kill - gen
            killsetMay = denseMapSub(killsetMay, gensetMay);
            killsetMust = denseMapSub(killsetMust, gensetMust);


            // mayout (transfer func):
            newMayPs.out = denseMapSub(ps.in, killsetMust);
            unionParams.clear();
            unionParams.push_back(newMayPs.out);
            unionParams.push_back(gensetMay);
            newMayPs.out = mergeDenseMaps(unionParams);


            //mayin (transfer func):
            newMustPs.mustout = denseMapSub(ps.mustin, killsetMay);
            unionParams.clear();
            unionParams.push_back(newMustPs.mustout);
            unionParams.push_back(gensetMust);
            newMustPs.mustout = mergeDenseMaps(unionParams);


            if (ps.out != newMayPs.out || ps.mustout != newMustPs.mustout) {
                ps.out = newMayPs.out;
                ps.mustout = newMustPs.mustout;
                std::vector<Instruction*> succInstructions = getSuccessors(Ins, F);
                for (int j = 0; j < (int)succInstructions.size(); j++) {
                    worklist.push_back(succInstructions[j]);
                }
            }
            st[Ins] = ps;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double analysisTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        outs() << "Per-program point point-to sets [flow-sensitive]: \n";
        for (auto* BB : bbs) {
            outs() << "Basic Block: " << BB->getName() << " \n";
            outs() << "IN[] Set: \n";
            prettyPrintPointState(outs(), st[&BB->front()].in, abstractObjects);
            outs() << "OUT[] Set: \n";
            prettyPrintPointState(outs(), st[&BB->back()].out, abstractObjects);
            outs() << "\n";
        }

        reportMetricsPerPoint(outs(), F, order, st, abstractObjects.size(),
                              worklist.size(), analysisTimeMs, "flow-sensitive");

        return PreservedAnalyses::all();
    }
};


// =============================================================================
// Pass 2: flow-insensitive Andersen baseline
//
// One global points-to map per function. All stores are weak updates (merge).
// We iterate over the instruction list applying inclusion constraints until
// the map stops changing. This is the textbook Andersen formulation reduced
// to a fixed-point loop.
// =============================================================================
struct FlowInsensitivePointsTo : PassInfoMixin<FlowInsensitivePointsTo> {

    // Apply one pass over all instructions, accumulating into ptsMap.
    // Returns true iff the map changed during this pass.
    static bool processOnce(Function& F,
                            DenseMap<Value*, BitVector>& ptsMap,
                            const std::vector<Value*>& abstractObjects) {
        bool changed = false;
        const unsigned N = abstractObjects.size();

        auto getPts = [&](Value* v) -> BitVector {
            auto it = ptsMap.find(v);
            if (it == ptsMap.end() || it->second.size() != N) {
                return BitVector(N, false);
            }
            return it->second;
        };
        auto setPts = [&](Value* v, const BitVector& bv) {
            auto it = ptsMap.find(v);
            if (it == ptsMap.end()) {
                ptsMap[v] = bv;
                if (bv.count() > 0) changed = true;
            } else {
                BitVector merged = it->second;
                BitVector before = merged;
                merged |= bv;
                if (merged != before) {
                    it->second = merged;
                    changed = true;
                }
            }
        };

        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* Load = dyn_cast<LoadInst>(&I)) {
                    if (!Load->getType()->isPointerTy()) continue;
                    // p = *q  =>  for each o in pts(q): pts(p) ⊇ pts(o)
                    Value* q = Load->getPointerOperand();
                    auto qIsAbstract = std::find(abstractObjects.begin(),
                                                 abstractObjects.end(), q);
                    BitVector ptsQ;
                    if (qIsAbstract != abstractObjects.end()) {
                        // q itself is the abstract location
                        ptsQ = BitVector(N, false);
                        ptsQ.set(static_cast<unsigned>(qIsAbstract - abstractObjects.begin()));
                    } else {
                        ptsQ = getPts(q);
                    }
                    // For each o in ptsQ, merge pts(o) into pts(p).
                    BitVector accum(N, false);
                    for (unsigned i = 0; i < ptsQ.size(); ++i) {
                        if (!ptsQ.test(i)) continue;
                        BitVector ptsO = getPts(abstractObjects[i]);
                        accum |= ptsO;
                    }
                    setPts(Load, accum);
                }
                else if (auto* Store = dyn_cast<StoreInst>(&I)) {
                    if (!Store->getValueOperand()->getType()->isPointerTy()) continue;
                    Value* v = Store->getValueOperand();
                    Value* p = Store->getPointerOperand();

                    // Compute pts(v).
                    BitVector ptsV(N, false);
                    auto vIsAbstract = std::find(abstractObjects.begin(),
                                                 abstractObjects.end(), v);
                    if (vIsAbstract != abstractObjects.end()) {
                        ptsV.set(static_cast<unsigned>(vIsAbstract - abstractObjects.begin()));
                    } else {
                        ptsV = getPts(v);
                    }

                    // Determine target set.
                    auto pIsAbstract = std::find(abstractObjects.begin(),
                                                 abstractObjects.end(), p);
                    if (pIsAbstract != abstractObjects.end()) {
                        // store v, ptr <abstract>: pts(p) ⊇ pts(v)
                        setPts(p, ptsV);
                    } else {
                        // *p = v : for each o in pts(p): pts(o) ⊇ pts(v)
                        BitVector ptsP = getPts(p);
                        for (unsigned i = 0; i < ptsP.size(); ++i) {
                            if (!ptsP.test(i)) continue;
                            setPts(abstractObjects[i], ptsV);
                        }
                    }
                }
                // No GEP / address-of explicit instruction at this IR level;
                // address-of is implicit via abstract objects (alloca/global)
                // appearing as store value operands, already handled above.
            }
        }
        return changed;
    }

    PreservedAnalyses run(Function& F, FunctionAnalysisManager& /*AM*/) {
        std::vector<Value*> abstractObjects = collectAbstractObjects(F);

        // Count program points up front for parity with flow-sensitive metrics.
        std::size_t numProgramPoints = 0;
        for (auto& BB : F) numProgramPoints += BB.size();

        DenseMap<Value*, BitVector> ptsMap;

        auto t0 = std::chrono::high_resolution_clock::now();

        std::size_t iterations = 0;
        bool changed = true;
        while (changed) {
            iterations++;
            changed = processOnce(F, ptsMap, abstractObjects);
            // Safety net: cap iterations far above any realistic fixpoint.
            if (iterations > 10000) break;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double analysisTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        outs() << "Function-level point-to map [flow-insensitive]: \n";
        outs() << "Function: " << F.getName() << "\n";
        prettyPrintMap(outs(), ptsMap, abstractObjects);

        reportMetricsSingle(outs(), F, ptsMap, abstractObjects.size(),
                            numProgramPoints, iterations, analysisTimeMs,
                            "flow-insensitive");

        return PreservedAnalyses::all();
    }
};

} // end namespace


// =============================================================================
// Pass Registration
// Usage:
//   opt -load-pass-plugin=unifiedpass.so \
//       -passes='flow-sensitive-points-to-analysis'   input.bc
//   opt -load-pass-plugin=unifiedpass.so \
//       -passes='flow-insensitive-points-to-analysis' input.bc
// =============================================================================
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "UnifiedPass", "v1.0", [](PassBuilder& PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager& FPM,
                       ArrayRef<PassBuilder::PipelineElement>) -> bool {
                        if (Name == "flow-sensitive-points-to-analysis") {
                            FPM.addPass(FlowSensitivePointsTo());
                            return true;
                        }
                        if (Name == "flow-insensitive-points-to-analysis") {
                            FPM.addPass(FlowInsensitivePointsTo());
                            return true;
                        }
                        return false;
                    });
            }};
}