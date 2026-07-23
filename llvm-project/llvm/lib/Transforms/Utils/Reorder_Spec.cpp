//===-- Reorder.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


#include "llvm/Transforms/Utils/Reorder_Spec.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasAnalysisEvaluator.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/AliasGraph.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
// #include "llvm/Analysis/ObjectSizeOffsetVisitor.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/CFG.h"
#include <queue>
#include <fstream>
#include <regex>
#include <string>
#include <iostream>
#include <mysql/mysql.h>
#include <omp.h>
#include "llvm/ADT/Statistic.h"
 using namespace llvm;
 #define DEBUG_TYPE "reorder_load"
// #define DEBUG_PRINT;
// #define DEBUG_DISTANCE;
// #define DEBUG_PRINT_INTERPROCEDURAL;
   //create global variables for wasted and reorder counts
  STATISTIC(NumSuccessfulReorders, "Number of successful reorders");
  STATISTIC(NumWastedReorders, "Number of wasted reorders");
  STATISTIC(NumAliasingFails_Spec, "Number of failed to due Aliasing");
  STATISTIC(NumFusiblePairs_Spec, "Number of Fusion pairs Identified");
  STATISTIC(NumCallsReorder_Spec, "Failed due to calls");
  STATISTIC(NumUDHeadReorder_Spec, "Number of Use-def contains head wasted");
 int globalWasted_Spec = 0;
 int globalReorder_Spec = 0;
 int globaldiffchainedgraphaa_Spec = 0;
 int run_globaldiffcahinedgraphaa_call_Spec = 0;
 int globalAcrossCallReorder_Spec = 0;
 int globalAcrossCallWasted_Spec = 0;
 int globaldiffchainedsvfgraphaa_Spec = 0;
 int globaldiffchainedsvfgraphaa_call_Spec = 0;
 int globaldiffchainedsvf_Spec = 0;
 static cl::opt<bool> EnableSpeculativeReordering_Spec(
  "enable-spec-reordering-spec", cl::init(false), cl::Hidden,
  cl::desc("Enable Speculative Reordering."));

  static cl::opt<bool> EnableAACallReordering_Spec(
    "enable-aa-call-reordering-spec", cl::init(false), cl::Hidden,
    cl::desc("Enable AA Call Reordering."));

  static cl::opt<bool> EnableAcrossCallReordering_Spec(
    "enable-across-call-reordering-spec", cl::init(false), cl::Hidden,
    cl::desc("Enable Reordering across calls."));

  static cl::opt<int> ReorderDistance_Spec(
    "reorder-distance-spec", cl::init(10000000000), cl::Hidden,
    cl::desc("Reorder distance."));
 
static cl::opt<bool> EnableQueryMode(
    "enable-query-mode-spec", cl::init(false), cl::Hidden,
    cl::desc("Enable query mode for specific pointer pairs"));

static cl::opt<bool> EnablePrecomputedAliasResults(
    "enable-precomputed-alias-results-spec", cl::init(false), cl::Hidden,
    cl::desc("Enable use of precomputed alias results from file"));

static cl::opt<bool> EnablePrecomputedAliasResults_interprocedural(
    "enable-precomputed-alias-results-spec-interprocedural", cl::init(false), cl::Hidden,
    cl::desc("Enable use of precomputed interprocedural results from file")
); 

static cl::opt<bool> ChainGraphaawithSVF(
    "chain-graphaawith-svf-spec", cl::init(false), cl::Hidden,
    cl::desc("Chain Graph AA with SVF Graph AA"));

static cl::opt<bool> EnableGraphAA(
    "enable-graph-aa-spec", cl::init(false), cl::Hidden,
    cl::desc("Enable Graph AA for alias analysis"));

/// Result type for pointer difference
struct PtrDiffResult {
    bool IsConstant;
    APInt ConstantValue;     // valid if IsConstant == true
    const SCEV *Symbolic;    // valid if IsConstant == false
};

static PtrDiffResult computePointerDiff(Value *PtrA,
                                        Value *PtrB,
                                        const DataLayout &DL,
                                        ScalarEvolution &SE,
                                        TargetLibraryInfo &TLI) {
    PtrDiffResult Result;

    //========================
    // 1. FAST PATH
    //========================
    APInt OffsetA(DL.getIndexTypeSizeInBits(PtrA->getType()), 0);
    APInt OffsetB(DL.getIndexTypeSizeInBits(PtrB->getType()), 0);

    Value *BaseA = PtrA->stripAndAccumulateConstantOffsets(DL, OffsetA, true);
    Value *BaseB = PtrB->stripAndAccumulateConstantOffsets(DL, OffsetB, true);

    // Stronger base check
    Value *ObjA = llvm::getUnderlyingObject(BaseA);
    Value *ObjB = llvm::getUnderlyingObject(BaseB);

    if (ObjA == ObjB) {
        // Address space check
        unsigned ASA = cast<PointerType>(BaseA->getType())->getAddressSpace();
        unsigned ASB = cast<PointerType>(BaseB->getType())->getAddressSpace();

        if (ASA == ASB) {
            unsigned IdxWidth = DL.getIndexSizeInBits(ASA);
            OffsetA = OffsetA.sextOrTrunc(IdxWidth);
            OffsetB = OffsetB.sextOrTrunc(IdxWidth);

            Result.IsConstant = true;
            Result.ConstantValue = OffsetB - OffsetA;
            return Result;
        }
    }

    //========================
    // 2. OBJECT SIZE OFFSET VISITOR
    //========================
    LLVMContext &Ctx = PtrA->getContext();
    ObjectSizeOffsetVisitor Eval(DL, &TLI, Ctx);

    auto PairA = Eval.compute(PtrA);
    auto PairB = Eval.compute(PtrB);

    if (ObjectSizeOffsetVisitor::knownOffset(PairA) &&
    ObjectSizeOffsetVisitor::knownOffset(PairB)) {
        const APInt &BaseA2 = PairA.second;
        const APInt &BaseB2 = PairB.second;

        Value *ObjA2 = llvm::getUnderlyingObject(PtrA);
        Value *ObjB2 = llvm::getUnderlyingObject(PtrB);

        if (ObjA2 == ObjB2) {
          Result.IsConstant = true;
          Result.ConstantValue = APInt(BaseA2.getBitWidth(),BaseB2.getSExtValue() - BaseA2.getSExtValue());
          return Result;
        }
    }

    //========================
    // 3. SCEV FALLBACK
    //========================
    const SCEV *SCEVA = SE.getSCEV(PtrA);
    const SCEV *SCEVB = SE.getSCEV(PtrB);
    const SCEV *Diff = SE.getMinusSCEV(SCEVB, SCEVA);

    if (const SCEVConstant *C = dyn_cast<SCEVConstant>(Diff)) {
        Result.IsConstant = true;
        Result.ConstantValue = C->getAPInt();
        return Result;
    }

    Result.IsConstant = false;
    Result.Symbolic = Diff;
    return Result;
}

static bool sideaffectcheck(Instruction *I) {
  unsigned opcode = I->getOpcode();
  switch (opcode) {
  default: return false;
  case Instruction::Fence: // FIXME: refine definition of mayWriteToMemory
  case Instruction::VAArg:
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
    return true;
  // case Instruction::Call:
  // case Instruction::Invoke:
  case Instruction::CallBr:
    return !cast<CallBase>(I)->onlyReadsMemory();
  case Instruction::Load:
    return !cast<LoadInst>(I)->isUnordered();
  }
}

PreservedAnalyses ReorderPass_Spec::runonFunction(Function &F, ModuleAnalysisManager &MAM, FunctionAnalysisManager &FAM) {
  // auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  Module &M = *F.getParent();
  std::string ModuleName = M.getName().str();
  std::string FunctionName = F.getName().str();
  AliasAnalysis &AA = FAM.getResult<AAManager>(F);
  BasicAAResult &BasicAAR = FAM.getResult<BasicAA>(F);
  auto &DA = FAM.getResult<DependenceAnalysis>(F);
  auto &DL = F.getParent()->getDataLayout();
  auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  LLVMContext &Context = F.getContext();
  TargetLibraryInfo &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
  // GraphAAResult &GraphAAR = MAM.getResult<GraphAA>( *(F.getParent()));
  auto &GlobalsAAR = MAM.getResult<GlobalsAA>( *(F.getParent()));
  SimpleAAQueryInfo SimpleAAQI (AA);
  std::vector<Instruction *> defChain;
  SetVector <Instruction *> Int_ins;
  SetVector <Instruction *> call_ins;
  SetVector <Instruction *> Int_dep;
  SetVector <Instruction *> useChain;
  SetVector <Instruction *> useChain_ordered;
  SetVector <Instruction *> prev_load_usechain;
  SetVector <Instruction *> noDepChain;
  Instruction *int_mem_op = nullptr;
  Instruction *currentInst = nullptr;
  LoadInst *prevLoadInst = nullptr;
  std::queue<Instruction *> worklist;
  std::queue<Instruction *> worklist_prev;
  int distance = 0;
    // Transfer elements from set to vector
    //std::vector<Instruction *> tempVector(useChain.begin(), useChain.end());
  int Val;
  Instruction *startInst;
  int wasted = 0;
  int reorder = 0;
  bool UseChain_contains_prevLoad = false;
  bool UseChain_contains_currLoad = false;
  bool Aliases_with_prevLoad = false;
  bool Aliases_with_currLoad = false;
  bool successfully_reordered = false;
  bool contains_call_instruction = false;
  bool store_detected_call = false;
  bool call_inbetween = false; //comment
  bool call_in_catalyst_mod_load = false;
      for (auto &BB : F) {
        LoadInst *prevLoadInst = nullptr;
        call_inbetween = false; //comment
        #ifdef DEBUG_PRINT
        errs() << "next BB " << "\n";
        #endif
        #ifdef DEBUG_PRINT
        errs() << "Analyzing Basic Block: " << BB.getName() << "\n";
        #endif
            BasicBlock::iterator it_bb = BB.begin();
            // BasicBlock::iterator end = BB.end();
            for(it_bb = BB.begin(); it_bb != BB.end();++it_bb){
              Instruction &I = *it_bb;
              if(it_bb == BB.end() && prevLoadInst != nullptr){
                #ifdef DEBUG_PRINT
                errs() << "End of BB with pending prevLoadInst" << *prevLoadInst << "\n";
                #endif
                it_bb = prevLoadInst->getIterator();
                // ++it_bb;
                prevLoadInst = nullptr;
                continue;
              }
              if((I.mayThrow() || !I.willReturn() || sideaffectcheck(&I) || I.isVolatile() || I.isAtomic()) && prevLoadInst != nullptr){
                it_bb = prevLoadInst->getIterator();
                // ++it_bb;
                prevLoadInst = nullptr;
                continue;
              }

              if (isa<LoadInst>(&I) && prevLoadInst != nullptr) {
                if (I.getType()->isVectorTy()) {
                    #ifdef DEBUG_PRINT
                    errs() << "Skipping vector load instruction: " << I << "\n";
                    #endif
                    // ++it_bb;
                    continue;
                }
              }

              if(!EnableAcrossCallReordering_Spec && prevLoadInst != nullptr)
              {
                // errs() << "Encountered call instruction, resetting prevLoadInst " << dyn_cast<CallInst>(&I) << "\n";
                // errs() << "PrevLoadInst before reset: " << *prevLoadInst << "\n";
                if(CallBase *CallInst_temp = dyn_cast<CallBase>(&I)){
                  #ifdef DEBUG_PRINT
                  errs() << "Encountered call instruction, resetting prevLoadInst" << *cast<CallBase>(&I) << "\n";
                  errs() << "Call instruction found in between loads" << *CallInst_temp << "\n";
                  #endif
                  call_inbetween = true; //comment
                  it_bb = prevLoadInst->getIterator();
                  // ++it_bb;
                  prevLoadInst = nullptr;
                  distance = 0;
                  NumCallsReorder_Spec++;
                  continue;
                }
              } 
              else if(EnableAcrossCallReordering_Spec && prevLoadInst != nullptr){
                #ifdef DEBUG_PRINT
                errs() << "Across call reordering enabled" << "\n";
                #endif
              }else{
                if(isa<CallInst>(&I) && prevLoadInst != nullptr){
                  assert(false && "This condition should not be hit, either across call reordering is enabled or prevLoadInst should be null");
                  // #ifdef DEBUG_PRINT
                  errs() << "Unexpected condition in across call reordering\n";
                  errs() << "PrevLoadInst: " << *prevLoadInst << "\n";
                  errs() << "Call Instruction: " << *cast<CallInst>(&I) << "\n";
                  // #endif
                }
              }
        //for (auto &I : BB) {
          if (LoadInst *loadInst = dyn_cast<LoadInst>(&I)) {
            successfully_reordered = false;
            // Print the def chain leading to the nextLoadInst
            if (prevLoadInst) {
              // successfully_reordered = false;
              #ifdef DEBUG_PRINT
              errs() << "Previous(Head) Load Instruction: " << *prevLoadInst << "\n";
              errs() << "Found current(Tail) Load Instruction: " << *loadInst << "\n";
              #endif
              currentInst = loadInst;
              startInst = prevLoadInst;
              Value *curLoadPtr = loadInst->getPointerOperand();
              Value *prevLoadPtr = prevLoadInst->getPointerOperand();
              Type *ElemTyA = getLoadStoreType(loadInst);
              // PtrDiffResult PtrDiff = nullptr;
              // Value *scurLoadPtr = loadInst->getPointerOperand();
              // Value *sprevLoadPtr = prevLoadInst->getPointerOperand();
              // BasePointer_curLoadPtr = dyn_cast<SCEVUnknown>(curLoadPtr);
              // BasePointer_prevLoadPtr = dyn_cast<SCEVUnknown>(prevLoadPtr);
              #ifdef DEBUG_PRINT
              errs() << "Tail load ptr :" << *curLoadPtr << " Head load ptr :" << *prevLoadPtr << "\n";
              #endif
              PtrDiffResult Diff = computePointerDiff(curLoadPtr, prevLoadPtr, DL, SE, TLI);

              if (Diff.IsConstant) {
                  int64_t ByteDiff = Diff.ConstantValue.getSExtValue();

                  // Example: cache-line reasoning
                  const int64_t CacheLineSize = 64;
                  int64_t LineDistance = ByteDiff / CacheLineSize;
                  #ifdef DEBUG_PRINT
                  errs() << "Byte diff = " << ByteDiff << "\n";
                  errs() << "Cache-line distance = " << LineDistance << "\n";
                  #endif
                  if(std::abs(LineDistance) >= 2){
                    // errs() << "Instructions Alias curLoad: " << *loadInst << " prev load :" << *prevLoadInst << "\n";
                    currentInst = nullptr;
                    loadInst = nullptr;
                    // it_bb++;
                    continue;
                  }else{
                    NumFusiblePairs_Spec++;
                    #ifdef DEBUG_PRINT
                    errs() << "Can be fused in the hardware" << "\n";
                    #endif
                  }
              } else {
                  #ifdef DEBUG_PRINT
                  errs() << "Symbolic diff: " << *Diff.Symbolic << "\n";
                  #endif
                  currentInst = nullptr;
                  loadInst = nullptr;
                  // it_bb++;
                  continue;
              }
              #ifdef DEBUG_PRINT
              errs() << "Def Chain leading to: " << *loadInst << "\n";
              errs() << "Prev load Inst: " << *prevLoadInst << "\n";
              #endif
                while (currentInst && currentInst != startInst) {
                    defChain.push_back(currentInst);
                    Int_ins.insert(currentInst);
                    currentInst = currentInst->getPrevNode();
                }

                if (currentInst == startInst) {
                    defChain.push_back(currentInst);
                    Int_ins.insert(currentInst);
                    //Print the def chain in reverse order
                    for (auto it = defChain.rbegin(); it != defChain.rend(); ++it){
                      #ifdef DEBUG_PRINT
                      errs() << "Instructions in between" << *(*it) << "\n";
                      #endif
                      distance++;
                        if(CallBase *callInst = dyn_cast<CallBase>(*it)){
                          assert(EnableAcrossCallReordering_Spec && "Across call reordering should be enabled");
                        }
                    }
                    if(call_in_catalyst_mod_load){
                      distance = 0;
                      globalAcrossCallWasted_Spec++;
                      run_globalAcrossCallWasted_Spec++;
                      call_in_catalyst_mod_load = false;
                      currentInst = nullptr;
                      loadInst = nullptr;
                      NumWastedReorders++;
                      // it_bb = prevLoadInst->getIterator();
                      // ++it_bb;
                      // prevLoadInst = nullptr;
                      distance = 0;
                      defChain.clear();
                      Int_ins.clear();
                      continue;
                    }
                    if((distance -2) > ReorderDistance_Spec){
                      currentInst = nullptr;
                      loadInst = nullptr;
                      it_bb = prevLoadInst->getIterator();
                      NumWastedReorders++;
                      // ++it_bb;
                      prevLoadInst = nullptr;
                      distance = 0;
                      defChain.clear();
                      Int_ins.clear();
                      continue;
                    }
                    Aliases_with_prevLoad = false;
                    useChain.clear();
                    useChain_ordered.clear();
                    prev_load_usechain.clear();
                    noDepChain.clear();
                    for (auto it = Int_ins.rbegin(); it != Int_ins.rend(); ++it){
                      if(Instruction *LI = dyn_cast<LoadInst>(*it)){

                        if(LI == loadInst){
                          worklist.push(LI);
                          worklist_prev.push(prevLoadInst);
                          while (!worklist.empty()) { 
                            Instruction *inst = worklist.front();
                            worklist.pop();
                            // Add the instruction to the use chain set
                            // useChain.insert(inst);
                            if(useChain.contains(inst))
                            {
                              useChain.remove(inst);
                              useChain.insert(inst);
                            }else{
                              useChain.insert(inst);                                
                            }
                            //errs() << "Int pop: " << *inst << "\n";
                            //errs() << "Int pop: " << inst << "\n";
                            if (inst == prevLoadInst) {
                              // Stop collecting the use chain at prevLoadInst
                              UseChain_contains_prevLoad = true;
                            }
                            // Iterate over the operands of the instruction
                            for (Value *operand : inst->operands()){
                              if (Instruction *useInst = dyn_cast<Instruction>(operand)) {
                                  #ifdef DEBUG_PRINT
                                    errs() << "Use: " << *useInst << "\n";
                                  #endif
                                if (Int_ins.contains(useInst)) {
                                  worklist.push(useInst);
                                }
                              }
                            }
                          }
                          for (auto it = Int_ins.rbegin(); it != Int_ins.rend(); ++it){
                            if(useChain.contains(*it)){
                              useChain_ordered.insert(*it);
                              #ifdef DEBUG_PRINT
                                errs() << "Use Ordered Chain Instruction: " << *(*it) << "\n";
                              #endif

                            }
                          }

                          while (!worklist_prev.empty()) {
                            Instruction *inst = worklist_prev.front();
                            worklist_prev.pop();

                            // Add the instruction to the use chain set
                            if(prev_load_usechain.contains(inst))
                            {
                              prev_load_usechain.remove(inst);
                              prev_load_usechain.insert(inst);
                            }else{
                              prev_load_usechain.insert(inst);
                            }
                            //errs() << "Int pop: " << *inst << "\n";
                            //errs() << "Int pop: " << inst << "\n";
                            if (inst == loadInst) {
                              // Stop collecting the use chain at prevLoadInst
                              UseChain_contains_currLoad = true;
                              // continue;
                            }
                            
                            for (Value *operand : inst->operands()){
                              if (Instruction *useInst = dyn_cast<Instruction>(operand)) {
                                  #ifdef DEBUG_PRINT
                                    errs() << "Use: " << *useInst << "\n";
                                  #endif
                                if (Int_ins.contains(useInst)) {
                                  worklist_prev.push(useInst);
                                }
                              }
                            }
                          }

                          for (Instruction *I :Int_ins){
                            if(!useChain.contains(I) && I != prevLoadInst && I != loadInst){
                              noDepChain.insert(I);
                              #ifdef DEBUG_PRINT
                              errs() << "No Dep: " << *I << "\n";
                              #endif
                            }
                          }

                          if(useChain.contains(prevLoadInst)){ //Int_ins.contains(LI2) && inst == prevLoadInst
                            UseChain_contains_prevLoad = true;
                            NumUDHeadReorder_Spec++;
                          }

                          if(prev_load_usechain.contains(loadInst)){
                            UseChain_contains_currLoad = true;
                          }
                        
                          if(!UseChain_contains_prevLoad){
                            for(Instruction *I : useChain){
                              assert((I->mayThrow() || !I->willReturn() || sideaffectcheck(I) || I->isVolatile()) == false && "Instructions in use chain should not have side effects");
                              #ifdef DEBUG_PRINT
                              errs() << "Use Chain Instruction: " << *I << "\n";
                              #endif
                              //Need to check Instruction alias head load and no-dep instructions which are before the instruction in the use chain
                              if(StoreInst *SI = dyn_cast<StoreInst>(I)){
                                AliasResult AR_prevLoad_Store = AA.alias(MemoryLocation::get(prevLoadInst), MemoryLocation::get(SI), SimpleAAQI);
                                if(AR_prevLoad_Store != AliasResult::NoAlias){
                                  Aliases_with_prevLoad = true;
                                }
                                for(Instruction *noDepInst : noDepChain){
                                  if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                    if(SI_int->comesBefore(SI)){
                                      AliasResult AR_UDchain_noDep_SS = AA.alias(MemoryLocation::get(SI_int), MemoryLocation::get(SI), SimpleAAQI);
                                      if(AR_UDchain_noDep_SS != AliasResult::NoAlias){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                  if(LoadInst *LI_int = dyn_cast<LoadInst>(noDepInst)){
                                    if(LI_int->comesBefore(SI)){
                                      AliasResult AR_UDchain_noDep_SL = AA.alias(MemoryLocation::get(LI_int), MemoryLocation::get(SI), SimpleAAQI);
                                      if(AR_UDchain_noDep_SL != AliasResult::NoAlias){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                  if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                    assert(EnableAcrossCallReordering_Spec && "Across call reordering should be enabled");
                                    if(CI_int->comesBefore(SI)){
                                      ModRefInfo DMD_UDchain_noDep_SC = AA.getModRefInfo(CI_int, MemoryLocation::get(SI), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_SC = GlobalsAAR.getModRefInfo(CI_int, MemoryLocation::get(SI), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_SC = (DMD_UDchain_noDep_SC == ModRefInfo::ModRef) ? GMD_UDchain_noDep_SC : DMD_UDchain_noDep_SC;
                                      if(OMD_UDchain_noDep_SC != ModRefInfo::NoModRef){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                }
                              }

                              if(LoadInst *LI = dyn_cast<LoadInst>(I)){
                                AliasResult AR_prevLoad = AA.alias(MemoryLocation::get(prevLoadInst), MemoryLocation::get(LI), SimpleAAQI);
                                
                                if(AR_prevLoad != AliasResult::NoAlias && LI != loadInst){
                                  //should never enter here
                                  // assert(false);
                                  // Aliases_with_prevLoad = true;
                                }
                                for(Instruction *noDepInst : noDepChain){
                                  if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                    if(SI_int->comesBefore(LI)){
                                      AliasResult AR_UDchain_noDep_LS = AA.alias(MemoryLocation::get(SI_int), MemoryLocation::get(LI), SimpleAAQI);
                                      if(AR_UDchain_noDep_LS != AliasResult::NoAlias){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                  if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                    assert(EnableAcrossCallReordering_Spec && "Across call reordering should be enabled");
                                    if(CI_int->comesBefore(LI)){
                                      ModRefInfo DMD_UDchain_noDep_LC = AA.getModRefInfo(CI_int, MemoryLocation::get(LI), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_LC = GlobalsAAR.getModRefInfo(CI_int, MemoryLocation::get(LI), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_LC = (DMD_UDchain_noDep_LC == ModRefInfo::ModRef) ? GMD_UDchain_noDep_LC : DMD_UDchain_noDep_LC;
                                      if(OMD_UDchain_noDep_LC != ModRefInfo::NoModRef){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                }
                              }

                              if(CallBase *CI = dyn_cast<CallBase>(I)){
                                assert(EnableAcrossCallReordering_Spec && "Across call reordering should be enabled");
                                ModRefInfo DMD_UDchain_noDep_CC1 = AA.getModRefInfo(CI, MemoryLocation::get(prevLoadInst), SimpleAAQI);
                                ModRefInfo GMD_UDchain_noDep_CC1 = GlobalsAAR.getModRefInfo(CI, MemoryLocation::get(prevLoadInst), SimpleAAQI);
                                ModRefInfo OMD_UDchain_noDep_CC1 = (DMD_UDchain_noDep_CC1 == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CC1 : DMD_UDchain_noDep_CC1;
                                if(OMD_UDchain_noDep_CC1 != ModRefInfo::NoModRef){
                                  Aliases_with_prevLoad = true;
                                }
                                for(Instruction *noDepInst : noDepChain){
                                  if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                    if(SI_int->comesBefore(CI)){
                                      ModRefInfo DMD_UDchain_noDep_CS = AA.getModRefInfo(CI, MemoryLocation::get(SI_int), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_CS = GlobalsAAR.getModRefInfo(CI, MemoryLocation::get(SI_int), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_CS = (DMD_UDchain_noDep_CS == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CS : DMD_UDchain_noDep_CS;
                                      if(OMD_UDchain_noDep_CS != ModRefInfo::NoModRef){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                  if(LoadInst *LI_int = dyn_cast<LoadInst>(noDepInst)){
                                    if(LI_int->comesBefore(CI)){
                                      ModRefInfo DMD_UDchain_noDep_CL = AA.getModRefInfo(CI, MemoryLocation::get(LI_int), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_CL = GlobalsAAR.getModRefInfo(CI, MemoryLocation::get(LI_int), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_CL = (DMD_UDchain_noDep_CL == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CL : DMD_UDchain_noDep_CL;
                                      if(OMD_UDchain_noDep_CL != ModRefInfo::NoModRef){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                  }
                                  if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                    if(CI_int->comesBefore(CI)){
                                      ModRefInfo DMD_UDchain_noDep_CC2 = AA.getModRefInfo(CI, CI_int, SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_CC2 = GlobalsAAR.getModRefInfo(CI, CI_int, SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_CC2 = (DMD_UDchain_noDep_CC2 == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CC2 : DMD_UDchain_noDep_CC2;
                                      if(OMD_UDchain_noDep_CC2 != ModRefInfo::NoModRef){
                                        Aliases_with_prevLoad = true; 
                                      }
                                    }
                                }
                              }
                            }
                          }
                          for(Instruction *noDepInst : noDepChain){
                            assert((noDepInst->mayThrow() || !noDepInst->willReturn() || sideaffectcheck(noDepInst) || noDepInst->isVolatile()) == false && "No-dep chain should not contain instructions with side effects");
                              if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                if(SI_int->comesBefore(loadInst)){
                                  AliasResult AR_noDep_tailLoad_S = AA.alias(MemoryLocation::get(SI_int), MemoryLocation::get(loadInst), SimpleAAQI);
                                  if(AR_noDep_tailLoad_S != AliasResult::NoAlias){
                                    Aliases_with_prevLoad = true; 
                                  }
                                }
                              }
                              // if(LoadInst *LI_int = dyn_cast<LoadInst>(noDepInst)){
                              //   if(LI_int->comesBefore(loadInst)){
                              //     AliasResult AR_noDep_tailLoad_L = AA.alias(MemoryLocation::get(LI_int), MemoryLocation::get(loadInst), SimpleAAQI);
                              //     if(AR_noDep_tailLoad_L != AliasResult::NoAlias){
                              //       Aliases_with_prevLoad = true; 
                              //     }
                              //   }
                              // }
                              if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                assert(EnableAcrossCallReordering_Spec && "Across call reordering should be enabled");
                                if(CI_int->comesBefore(loadInst)){
                                  ModRefInfo DMD_noDep_tailLoad_C = AA.getModRefInfo(CI_int, MemoryLocation::get(loadInst), SimpleAAQI);
                                  ModRefInfo GMD_noDep_tailLoad_C = GlobalsAAR.getModRefInfo(CI_int, MemoryLocation::get(loadInst), SimpleAAQI);
                                  ModRefInfo OMD_noDep_tailLoad_C = (DMD_noDep_tailLoad_C == ModRefInfo::ModRef) ? GMD_noDep_tailLoad_C : DMD_noDep_tailLoad_C;
                                  if(OMD_noDep_tailLoad_C != ModRefInfo::NoModRef){
                                    Aliases_with_prevLoad = true; 
                                  }
                                }
                              }
                            }
                            #ifdef DEBUG_PRINT
                            if(UseChain_contains_prevLoad){
                              errs() << "PrevLoad in the UseChain: " << "True" << "\n";
                            }else{
                              errs() << "PrevLoad in the UseChain: " << "False" << "\n";
                            }

                            if(UseChain_contains_currLoad){
                              errs() << "curLoad in the Prev Load UseChain: " << "True" << "\n";
                            }else{
                              errs() << "curLoad in the Prev Load UseChain: " << "False" << "\n";
                            }
                            #endif
                            #ifdef DEBUG_PRINT
                            if(LI == loadInst){
                              errs() << "User Chain for: " << *prevLoadInst << "\n";
                              for (auto it = prev_load_usechain.rbegin(); it != prev_load_usechain.rend(); ++it) {//for (Instruction *inst : useChain) {
                                Instruction *inst = *it;
                                errs() << "Inst in Pre Load Use Chain" << *inst << "\n";
                              }
                            }
                            #endif
                          }
                      }
                    }
                    #ifdef DEBUG_DISTANCE
                      errs() << "Distance" << (distance -2) << "\n";
                    #endif
                    }
                    if(Aliases_with_prevLoad || UseChain_contains_prevLoad){
                      if(Aliases_with_prevLoad){
                        NumAliasingFails_Spec++;
                      }
                      #ifdef DEBUG_PRINT
                      errs() << "Tail load: " << *loadInst << "\n";
                      errs() << "Head load: " << *prevLoadInst << "\n";
                      errs() << "Cannot reorder due to: " << Aliases_with_prevLoad << " Does UseChain contain previous load: " << UseChain_contains_prevLoad << "\n";
                      for(auto it = Int_ins.rbegin(); it != Int_ins.rend(); ++it){
                        //print use chain and alias instructions
                        errs() << "Inst in between: " << *(*it) << "\n";
                      }
                      errs() << "Current load is in the use chain of previous load but previous load is not in the use chain of current load, cannot reorder" << "\n";
                      #endif
                      currentInst = nullptr;
                      loadInst = nullptr;
                      distance = 0;
                      defChain.clear();
                      Int_ins.clear();
                      useChain.clear();
                      useChain_ordered.clear();
                      noDepChain.clear();
                      prev_load_usechain.clear();
                      Aliases_with_prevLoad = false;
                      UseChain_contains_prevLoad = false;
                      NumWastedReorders++;
                      continue;
                    }
                    for (auto it = Int_ins.rbegin(); it != Int_ins.rend(); ++it){
                      if(Instruction *LI = dyn_cast<LoadInst>(*it)){
                        if(LI == loadInst){
                          #ifdef DEBUG_PRINT
                          errs() << "Use Chain for tail load: " << *loadInst << "\n";
                          for (auto it = useChain.rbegin(); it != useChain.rend(); ++it) {//for (Instruction *inst : useChain) {
                            Instruction *inst = *it;
                            errs() << "Inst in Use Chain for tail load: " << *inst << "\n";
                          }
                          #endif
                            for (auto it = useChain_ordered.begin(); it != useChain_ordered.end(); ++it) {
                              Instruction *inst = *it;
                            //for (Instruction *inst : useChain) {
                              if(inst != prevLoadInst && inst == loadInst){
                                successfully_reordered = true;
                                reorder++;
                                inst->moveAfter(prevLoadInst);
                                #ifdef DEBUG_PRINT
                                errs() << "Ins move after Tail Load: " << *inst << "\n";
                                #endif
                              } else if(inst != prevLoadInst && inst != loadInst){
                                inst->moveBefore(prevLoadInst);
                                #ifdef DEBUG_PRINT
                                errs() << "Ins move before Tail Load: " << *inst << "\n";
                                #endif
                              }  
                            }
                        }
                      }
                    }
                }
              if(successfully_reordered){
                // it_bb = ++loadInst->getIterator();
                it_bb = loadInst->getIterator();
                prevLoadInst = nullptr;
                NumSuccessfulReorders++;
                call_inbetween = false; //comment
                successfully_reordered = false;
                currentInst = nullptr;
                loadInst = nullptr;
                globalReorder_Spec++;
                run_globalReorder_Spec++;
                distance = 0;
              }else {
                currentInst = nullptr;
                loadInst = nullptr;
                NumWastedReorders++;
                // it_bb++;
                globalWasted_Spec++;
                run_globalWasted_Spec++;
              }
              if(UseChain_contains_prevLoad){
                distance = 0;
              }
              #ifdef DEBUG_PRINT
              errs() << "--------" << "\n";
              errs() << "Resetting for next load" << "\n";
              errs() << "--------" << "\n";
              errs() << "\n";
              #endif
              defChain.clear();
              Int_ins.clear();
              Int_dep.clear();
              useChain.clear();
              useChain_ordered.clear();
              call_inbetween = false; //comment
              prev_load_usechain.clear();
              noDepChain.clear();
              UseChain_contains_prevLoad = false;
              Aliases_with_prevLoad = false;
              Aliases_with_currLoad = false;
              std::queue<Instruction *>().swap(worklist);
            }else{
              prevLoadInst = loadInst;
            }
            // if(successfully_reordered){
            //     prevLoadInst = nullptr;
            //     currentInst = nullptr;
            //     loadInst = nullptr;
            //     call_inbetween = false; //comment
            //     distance = 0;
            //   }else{
            //     currentInst = nullptr;
            //     loadInst = nullptr;
            //     it_bb++;
            //   }
              // if(!prevLoadInst){
              //   prevLoadInst = loadInst;
              // }
          }
          else if(CallInst *callinst = dyn_cast<CallInst>(&I)){
            call_ins.clear();
            if(!loadInst && prevLoadInst){
              #ifdef DEBUG_PRINT
              errs() << "Call Inst:" << I << "\n";
              #endif
              contains_call_instruction = true;
              Function *parentFunc = callinst->getFunction();
              if (CallInst *callInst = dyn_cast<CallInst>(&I)) {
                // Analyze instructions within the called function
                Function *calledFunc = callInst->getCalledFunction();
                if (calledFunc && !calledFunc->isDeclaration()) {
                    // Look for potential aliasing stores in the called function
                    for (BasicBlock &calledBB : *calledFunc) {
                        for (Instruction &calledI : calledBB) {
                          call_ins.insert(&calledI);
                        }
                      }
                }
              }
            }
          }
        }
      }
      #ifdef DEBUG_PRINT
      errs() << "Finished Function: " << F.getName() << "\n";
      errs() << "Number of Reorders: " << reorder << "\n";
      errs() << "Number of wasted: " << wasted << "\n";
      errs() << "Number of global wasted: " << globalWasted_Spec << "\n";
      errs() << "Number of successful reorders: " << globalReorder_Spec << "\n";
      errs() << "Number of across call wasted: " << globalAcrossCallWasted_Spec << "\n";
      errs() << "Number of global diff chained graph-aa: " << globaldiffchainedgraphaa_Spec << "\n";
      #endif
    return PreservedAnalyses::all();
 }

PreservedAnalyses ReorderPass_Spec::run(Module &M, ModuleAnalysisManager &AM) {
    // Initialize the analysis manager
    for(auto &F : M) {
      if (F.isDeclaration())
        continue;
      FunctionAnalysisManager &FAM =
          AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    // Run the reordering pass
      this->runonFunction(F, AM, FAM);
    }
    errs() << "Finished Module: " << M.getName() << "\n";
    errs() << "load_run_globalWasted_Spec: " << run_globalWasted_Spec << "\n";
    errs() << "load_run_globalReorder_Spec: " << run_globalReorder_Spec << "\n";
    errs() << "load_run_globalAcrossCallReorder_Spec: " << run_globalAcrossCallReorder_Spec << "\n";
    errs() << "load_run_globalAcrossCallWasted_Spec: " << run_globalAcrossCallWasted_Spec << "\n";
    errs() << "load_run_globaldiffchainedgraphaa_Spec: " << run_globaldiffchainedgraphaa_Spec << "\n";
    errs() << "load_run_globaldiffchainedsvf_Spec: " << run_globaldiffchainedsvf_Spec << "\n";
    errs() << "load_run_globaldiffchainedsvfgraphaa_Spec: " << run_globaldiffchainedsvfgraphaa_Spec << "\n";
    errs() << "load_run_intra_aa_svf_diff_SpecLoad: " << run_intra_aa_svf_diff_SpecLoad << "\n";
    // Return preserved analyses
    return PreservedAnalyses::all();
}