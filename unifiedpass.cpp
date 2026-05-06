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

static BitVector bitVectorSub(BitVector bv1, BitVector bv2) {
    if (bv1.size() != bv2.size()) return {};
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
static std::vector<Value*> getPointsToValue(const DenseMap<Value*, BitVector>& pointsToInfo,
                                            Value* pointer,
                                            const std::vector<Value*>& abstractObjects) {
    auto it = pointsToInfo.find(pointer);
    std::vector<Value*> rtn;
    if (it == pointsToInfo.end()) return rtn;
    const BitVector& bv = it->second;
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

    static DenseMap<Value*, BitVector> mergeDenseMaps(std::vector<DenseMap<Value*, BitVector>> maps) {
        DenseMap<Value*, BitVector> newMap;
        for (int i = 0; i < (int)maps.size(); i++) {
            for (auto& [key, value] : maps[i]) {
                auto it = newMap.find(key);
                if (it != newMap.end()) {
                    auto& newMapValue = it->second;
                    std::vector<BitVector> unionParams;
                    unionParams.push_back(newMapValue);
                    unionParams.push_back(value);
                    newMap[key] = meetUnion(unionParams);
                } else {
                    newMap.insert({key, value});
                }
            }
        }
        return newMap;
    }

    static DenseMap<Value*, BitVector> getInSet(Instruction* Ins,
                                                DenseMap<const Instruction*, PointState> st,
                                                int /*size*/) {
        Instruction* prevIns = Ins->getPrevNode();
        if (prevIns == nullptr) {
            if (Ins->getParent()->isEntryBlock()) {
                DenseMap<Value*, BitVector> emptyMap;
                return emptyMap;
            } else {
                std::vector<DenseMap<Value*, BitVector>> predOutSets;
                for (BasicBlock* pred : predecessors(Ins->getParent())) {
                    Instruction* lastIns = &pred->back();
                    predOutSets.push_back(st[lastIns].out);
                }
                return mergeDenseMaps(predOutSets);
            }
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
    static DenseMap<Value*, BitVector> transferFunc(Instruction* Ins, PointState ps,
                                                    std::vector<Value*> abstractObjects,
                                                    DominatorTree& DT, Function& F) {
        DenseMap<Value*, BitVector> outMap = ps.in;

        if (auto* Load = dyn_cast<LoadInst>(Ins)) {
            if (Load->getType()->isPointerTy()) {
                Value* pointerOperand = Load->getPointerOperand();
                auto it2 = std::find(abstractObjects.begin(), abstractObjects.end(), pointerOperand);
                if (it2 == abstractObjects.end()) {
                    std::vector<Value*> pointerTargets = getPointsToValue(outMap, pointerOperand, abstractObjects);
                    if (pointerTargets.size() == 1) {
                        pointerOperand = pointerTargets[0];
                    }
                }
                BitVector bv = outMap[Load];
                BitVector ptrBv = outMap[pointerOperand];
                std::vector<BitVector> unionParams;
                unionParams.push_back(bv);
                unionParams.push_back(ptrBv);
                outMap[Load] = meetUnion(unionParams);
            }
        }
        else if (auto* Store = dyn_cast<StoreInst>(Ins)) {
            if (Store->getValueOperand()->getType()->isPointerTy()) {
                Value* valueOp = Store->getValueOperand();
                Value* pointerOp = Store->getPointerOperand();
                BasicBlock* exitBlock = &F.back();

                std::vector<Value*> targets;
                auto it2 = std::find(abstractObjects.begin(), abstractObjects.end(), pointerOp);
                if (it2 != abstractObjects.end()) {
                    targets.push_back(pointerOp);
                } else if (auto* AddrLoad = dyn_cast<LoadInst>(pointerOp)) {
                    if (AddrLoad->getType()->isPointerTy()) {
                        targets = getPointsToValue(outMap, AddrLoad, abstractObjects);
                    }
                }

                BitVector newVal(abstractObjects.size(), false);
                auto it = std::find(abstractObjects.begin(), abstractObjects.end(), valueOp);
                if (it != abstractObjects.end()) {
                    newVal.set(static_cast<unsigned>(it - abstractObjects.begin()));
                } else {
                    auto vIt = outMap.find(valueOp);
                    if (vIt != outMap.end() && vIt->second.size() == abstractObjects.size()) {
                        newVal = vIt->second;
                    }
                }

                bool isStrong = (targets.size() == 1)
                                && DT.dominates(Ins->getParent(), exitBlock);

                for (Value* target : targets) {
                    if (isStrong) {
                        outMap[target] = newVal;
                    } else {
                        BitVector oldVal = outMap[target];
                        if (oldVal.size() != abstractObjects.size()) {
                            oldVal = BitVector(abstractObjects.size(), false);
                        }
                        std::vector<BitVector> u = {oldVal, newVal};
                        outMap[target] = meetUnion(u);
                    }
                }
            }
        }

        return outMap;
    }

    PreservedAnalyses run(Function& F, FunctionAnalysisManager& AM) {
        DominatorTree& DT = AM.getResult<DominatorTreeAnalysis>(F);

        std::vector<Value*> abstractObjects = collectAbstractObjects(F);

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
            ps.in = getInSet(Ins, st, abstractObjects.size());
            DenseMap<Value*, BitVector> newOut = transferFunc(Ins, ps, abstractObjects, DT, F);
            if (ps.out != newOut) {
                ps.out = newOut;
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
        for (int j = 0; j < (int)order.size(); j++) {
            outs() << "Program Point: " << *order[j] << " \n";
            prettyPrintPointState(outs(), st[order[j]], abstractObjects);
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