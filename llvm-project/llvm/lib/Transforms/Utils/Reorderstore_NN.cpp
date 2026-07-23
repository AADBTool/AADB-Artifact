//===-- Reorderstore_NN.cpp - Example Transformation --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

 
#include "llvm/Transforms/Utils/ReorderStore_NN.h"
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
  #define DEBUG_TYPE "reorder_store"
// #define DEBUG_PRINT;
// #define DEBUG_DISTANCE;
// #define DEBUG_PRINT_INTERPROCEDURAL;
  //create global variables for wasted and reorder counts
  STATISTIC(NumSuccessfulReordersStore_NN, "Number of successful reorders");
  STATISTIC(NumWastedReordersStore_NN, "Number of wasted reorders");
  STATISTIC(NumFusiblePairsStore_NN, "Number of fusible pairs Identified");
  STATISTIC(NumAliasingFailsStore_NN, "Number of failed to due Aliasing");
  STATISTIC(NumUDStoreWastedStore_NN, "Number of Use-def chain of Tail Store contain Store");
  STATISTIC(NumCallsReorderStore_NN, "Failed due to calls");
  STATISTIC(NumUDHeadReorderStore_NN, "Number of Use-def contains head wasted");
 int globalWasted_store_NN = 0;
 int globalReorder_store_NN = 0;
 int globaldiffchainedgraphaa_store_NN = 0;
 int run_globaldiffcahinedgraphaa_call_store_NN = 0;
 int globalAcrossCallReorder_store_NN = 0;
 int globalAcrossCallWasted_store_NN = 0;
 static cl::opt<bool> EnableSpeculativeReorderingStore_NN(
  "enable-spec-reordering-store-nn", cl::init(false), cl::Hidden,
  cl::desc("Enable Speculative Reordering."));

  static cl::opt<bool> EnableAACallReorderingStore_NN(
    "enable-aa-call-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Enable AA Call Reordering."));

  static cl::opt<bool> EnableAcrossCallReorderingStore_NN(
    "enable-across-call-reordering-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Enable Reordering across calls."));

  static cl::opt<bool> EnableUseDefChainCanContainStoreReorderingStore_NN(
    "enable-use-def-chain-can-contain-store-reordering-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Enable Reordering across calls."));

  static cl::opt<int> ReorderDistanceStore_NN(
    "reorder-distance-store-nn", cl::init(10000000000), cl::Hidden,
    cl::desc("Reorder distance."));
     
static cl::opt<bool> EnableQueryModeStore(
    "enable-query-mode-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Enable query mode for specific pointer pairs"));

static cl::opt<bool> EnablePrecomputedAliasResultsStore(
    "enable-precomputed-alias-results-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Enable use of precomputed alias results from file"));

static cl::opt<bool> EnablePrecomputedAliasResultsStore_interprocedural(
    "enable-precomputed-alias-results-store-interprocedural", cl::init(false), cl::Hidden,
    cl::desc("Enable use of precomputed interprocedural results from file")
);

static cl::opt<bool> ChainGraphaawithSVFStore(
    "chain-graphaawith-svf-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Chain Graph AA with SVF Graph AA"));

static cl::opt<bool> EnableGraphAAStore(
    "enable-graph-aa-store-nn", cl::init(false), cl::Hidden,
    cl::desc("Enable Graph AA for alias analysis"));

std::string parallel_query_tables_store(MYSQL* base_conn, const std::string& baseName, const std::string& key, MYSQL_ROW row) {
    int nTables = atoi(row[0]);
    std::string aaValue;
    bool found = false;

    omp_set_num_threads(8);

    #pragma omp parallel for shared(found)
    for (int i = 0; i < nTables; i++) {
        if (found) continue;  // Early skip if already found

        // Each thread needs its own connection
        MYSQL* conn = mysql_init(NULL);
        if (!mysql_real_connect(conn, "localhost", "root", "password", "Test", 0, NULL, 0)) {
            std::cerr << "Thread " << omp_get_thread_num()
                      << " failed to connect: " << mysql_error(conn) << "\n";
            mysql_close(conn);
            continue;
        }

        std::string tableName = baseName;
        tableName += "_" + std::to_string(i+1);

        std::string query = "SELECT aa FROM " + tableName +
                            " WHERE ptr_ptr_func = '" + key + "'";

        #ifdef DEBUG_PRINT
        errs() << "Executing query: " << query << "\n";
        #endif

        if (mysql_query(conn, query.c_str())) {
          #ifdef DEBUG_PRINT
            std::cerr << "SELECT failed: " << mysql_error(conn)
                      << " for table: " << tableName << "\n";
          #endif
            mysql_close(conn);
            continue;
        }

        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) {
            std::cerr << "mysql_store_result failed: " << mysql_error(conn)
                      << " for table: " << tableName << "\n";
            mysql_close(conn);
            continue;
        }

        MYSQL_ROW row_aa = mysql_fetch_row(res);
        if (row_aa && row_aa[0]) {
            {
                if (!found) {
                    aaValue = row_aa[0];
                    #ifdef DEBUG_PRINT
                    errs() << "Found record in " << tableName << ": " << aaValue << "\n";
                    #endif
                    found = true;
                }
            }
        } else {
            #ifdef DEBUG_PRINT
            errs() << "No matching record in table: " << tableName << "\n";
            #endif
        }

        mysql_free_result(res);
        mysql_close(conn);
    }

    if (!found) {
        errs() << "No result found in any table" << " key: " << key << ".\n";
    } else {
      #ifdef DEBUG_PRINT
        errs() << "Final aaValue = " << aaValue << "\n";
      #endif
    }
    return aaValue;
}

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

PreservedAnalyses ReorderPassStore_NN::runonFunction(Function &F, ModuleAnalysisManager &MAM, FunctionAnalysisManager &FAM) {
  Module &M = *F.getParent();
  std::string ModuleName = M.getName().str();
  std::string FunctionName = F.getName().str();
  AliasAnalysis &AA = FAM.getResult<AAManager>(F);
  BasicAAResult &BasicAAR = FAM.getResult<BasicAA>(F);
  auto &DA = FAM.getResult<DependenceAnalysis>(F);
  auto &DL = F.getParent()->getDataLayout();
  auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  // GraphAAResult &GraphAAR = MAM.getResult<GraphAA>( *(F.getParent()));
  auto &GlobalsAAR = MAM.getResult<GlobalsAA>( *(F.getParent()));
  SimpleAAQueryInfo SimpleAAQI (AA);
  TargetLibraryInfo &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
  std::vector<Instruction *> defChain;
  SetVector <Instruction *> Int_ins;
  SetVector <Instruction *> call_ins;
  SetVector <Instruction *> Int_dep;
  SetVector <Instruction *> useChain;
  SetVector <Instruction *> useChain_ordered;
  SetVector <Instruction *> prev_store_usechain;
  SetVector <Instruction *> noDepChain;
  Instruction *int_mem_op = nullptr;
  Instruction *currentInst = nullptr;
  StoreInst *prevStoreInst = nullptr;
  std::queue<Instruction *> worklist;
  std::queue<Instruction *> worklist_prev;
  int distance = 0;
    // Transfer elements from set to vector
    //std::vector<Instruction *> tempVector(useChain.begin(), useChain.end());
  int Val;
  Instruction *startInst;
  int wasted = 0;
  int reorder = 0;
  bool UseChain_contains_prevStore = false;
  bool UseChain_contains_currStore = false;
  bool Aliases_with_prevStore = false;
  bool Aliases_with_currStore = false;
  bool successfully_reordered = false;
  bool contains_call_instruction = false;
  bool load_detected_in_call = false;
  bool call_inbetween = false; //comment
  bool call_in_catalyst_modref_store = false;
  for (auto &BB : F) {
    StoreInst *prevStoreInst = nullptr;
    call_inbetween = false; //comment
    #ifdef DEBUG_PRINT
    errs() << "next BB " << "\n";
    errs() << "Analyzing Basic Block: " << BB.getName() << "\n";
    #endif
    BasicBlock::iterator it_bb = BB.begin();
        // BasicBlock::iterator end = BB.end();
        for(it_bb = BB.begin(); it_bb != BB.end();++it_bb){
          Instruction &I = *it_bb;
          if(it_bb == BB.end() && prevStoreInst != nullptr){
            #ifdef DEBUG_PRINT
            errs() << "End of BB with pending prevStoreInst" << *prevStoreInst << "\n";
            #endif
            it_bb = prevStoreInst->getIterator();
            // ++it_bb;
            prevStoreInst = nullptr;
            continue;
          }
          if((I.mayThrow() || !I.willReturn() || sideaffectcheck(&I) || I.isVolatile() || I.isAtomic()) && prevStoreInst != nullptr){ //(I.mayHaveSideEffects() && prevStoreInst != nullptr)
            it_bb = prevStoreInst->getIterator();
            // ++it_bb;
            prevStoreInst = nullptr;
            continue;
          }
          if (isa<StoreInst>(&I) && prevStoreInst != nullptr) {
            Value *storedValue = cast<StoreInst>(&I)->getValueOperand();
            Type *valType = storedValue->getType();
            if (valType->isVectorTy() || valType->isArrayTy()) {
              #ifdef DEBUG_PRINT
              errs() << "Skipping vector instruction: " << I << "\n";
              #endif
              continue;
            }
          }

          if(!EnableAcrossCallReorderingStore_NN && prevStoreInst != nullptr)
          {
            if(CallBase *CallInst_temp = dyn_cast<CallBase>(&I)){
              #ifdef DEBUG_PRINT
              errs() << "Encountered call instruction, resetting prevStoreInst" << *CallInst_temp << "\n";
              #endif
              call_inbetween = true; //comment
              it_bb = prevStoreInst->getIterator();
              // ++it_bb;
              prevStoreInst = nullptr;
              distance = 0;
              NumCallsReorderStore_NN++;
              continue;
            }
          }else{
            #ifdef DEBUG_PRINT
            errs() << "Across call reordering enabled" << "\n";
            #endif
          }
      //for (auto &I : BB) {
          if (StoreInst *storeInst = dyn_cast<StoreInst>(&I)) {
            successfully_reordered = false;
            // Print the def chain leading to the nextLoadInst
    
            if (prevStoreInst) {
              // successfully_reordered = false;
              // errs() << "Entering 1 curstore" << *storeInst << "\n";
              // errs() << "Entering 2 Prevstore:" << *prevStoreInst << "\n";
              #ifdef DEBUG_PRINT
              errs() << "Previous(Head) Store Instruction: " << *prevStoreInst << "\n";
              errs() << "Found current(Tail) Store Instruction: " << *storeInst << "\n";
              #endif
              currentInst = storeInst;
              startInst = prevStoreInst;
              Value *curStorePtr = storeInst->getPointerOperand();
              Value *prevStorePtr = prevStoreInst->getPointerOperand();
              Type *ElemTyA = getLoadStoreType(storeInst);
              PtrDiffResult Diff = computePointerDiff(curStorePtr, prevStorePtr, DL, SE, TLI);

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
                    // errs() << "Instructions Alias curStore: " << *storeInst << " prev store :" << *prevStoreInst << "\n";
                    currentInst = nullptr;
                    storeInst = nullptr;
                    // it_bb++;
                    continue;
                  }else{
                    NumFusiblePairsStore_NN++;
                    #ifdef DEBUG_PRINT
                    errs() << "Can be fused in the hardware" << "\n";
                    #endif
                  }
              } else {
                  #ifdef DEBUG_PRINT
                  errs() << "Symbolic diff: " << *Diff.Symbolic << "\n";
                  #endif
                  currentInst = nullptr;
                  storeInst = nullptr;
                  // it_bb++;
                  continue;
              }
              #ifdef DEBUG_PRINT
              errs() << "Def Chain leading to: " << *storeInst << "\n";
              errs() << "Prev store Inst: " << *prevStoreInst << "\n";
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
                      errs() << "Catalyst Instruction: " << *(*it) << "\n";
                      #endif
                      distance++;
                      if(dyn_cast<CallBase>(*it)){
                        if(CallBase *callInst = dyn_cast<CallBase>(*it)){
                          assert(EnableAcrossCallReorderingStore_NN && "Across call reordering should be enabled");
                        }
                      }
                    }
                    if(call_in_catalyst_modref_store){
                      distance = 0;
                      call_in_catalyst_modref_store = false;
                      currentInst = nullptr;
                      storeInst = nullptr;
                      // it_bb = prevStoreInst->getIterator();
                      // ++it_bb;
                      NumWastedReordersStore_NN++;
                      // prevStoreInst = nullptr;
                      defChain.clear();
                      Int_ins.clear();
                      continue;
                    }
                    if((distance -2) > ReorderDistanceStore_NN){
                      currentInst = nullptr;
                      storeInst = nullptr;
                      it_bb = prevStoreInst->getIterator();
                      // ++it_bb;
                      prevStoreInst = nullptr;
                      NumWastedReordersStore_NN++;
                      distance = 0;
                      defChain.clear();
                      Int_ins.clear();
                      continue;
                    }
                    Aliases_with_prevStore = false;
                    useChain.clear();
                    useChain_ordered.clear();
                    prev_store_usechain.clear();
                    noDepChain.clear();
                    for (auto it = Int_ins.rbegin(); it != Int_ins.rend(); ++it){
                      #ifdef DEBUG_PRINT
                      errs() << "Def Chain Instruction: " << *(*it) << "\n";
                      #endif
                      if(Instruction *LI = dyn_cast<StoreInst>(*it)){
                        #ifdef DEBUG_DISTANCE
                          errs() << "Distance" << (distance -2) << "\n";
                        #endif
                        if(LI == storeInst){
                          worklist.push(LI);
                          worklist_prev.push(prevStoreInst);
                          while (!worklist.empty()) { 
                            Instruction *inst = worklist.front();
                            worklist.pop();
                            // Add the instruction to the use chain set
                            if(useChain.contains(inst))
                            {
                                useChain.remove(inst);
                                useChain.insert(inst);
                            }else{
                                useChain.insert(inst);                                
                            }
                            if (inst == prevStoreInst) {
                              // Stop collecting the use chain at prevStoreInst
                              UseChain_contains_prevStore = true;
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
                            Instruction *prev_ins_users = prevStoreInst;
                            worklist_prev.pop();

                            // Add the instruction to the use chain set
                            if(prev_store_usechain.contains(inst))
                            {
                              prev_store_usechain.remove(inst);
                              prev_store_usechain.insert(inst);
                            }else{
                              prev_store_usechain.insert(inst);
                            }
                            //errs() << "Int pop: " << *inst << "\n";
                            //errs() << "Int pop: " << inst << "\n";
                            if (inst == storeInst) {
                              // Stop collecting the use chain at prevStoreInst
                              UseChain_contains_prevStore = true;
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
                            if(!useChain.contains(I) && I != prevStoreInst && I != storeInst){
                              noDepChain.insert(I);
                              #ifdef DEBUG_PRINT
                                errs() << "No Dep: " << *I << "\n";
                              #endif
                            }
                          }
                          if(useChain.contains(prevStoreInst)){ //Int_ins.contains(LI2) && inst == prevStoreInst
                            UseChain_contains_prevStore = true;
                            NumUDHeadReorderStore_NN++;
                          }

                          if(prev_store_usechain.contains(storeInst)){
                            UseChain_contains_currStore = true;
                          }

                          if(!UseChain_contains_prevStore){
                            for(Instruction *I : useChain){
                              assert((I->mayThrow() || !I->willReturn() || sideaffectcheck(I) || I->isVolatile() || I->isAtomic()) == false && "Instruction in use chain should not have side effects");
                              #ifdef DEBUG_PRINT
                              errs() << "Use Chain Instruction: " << *I << "\n";
                              #endif
                              //Need to check Instruction alias head load and no-dep instructions which are before the instruction in the use chain
                              if(StoreInst *SI = dyn_cast<StoreInst>(I)){
                                  if(SI != storeInst && EnableUseDefChainCanContainStoreReorderingStore_NN){
                                    assert(EnableUseDefChainCanContainStoreReorderingStore_NN && "Use-def chain cannot contain store reordering should be enabled");
                                    AliasResult AR_prevStore_UD_store = AA.alias(MemoryLocation::get(prevStoreInst), MemoryLocation::get(SI), SimpleAAQI);
                                    if(AR_prevStore_UD_store != AliasResult::NoAlias && SI != storeInst){
                                      Aliases_with_prevStore = true;
                                    }
                                    for(Instruction *noDepInst : noDepChain){
                                      if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                        if(SI_int->comesBefore(SI) && SI != storeInst){
                                          AliasResult AR_UDchain_noDep_SS = AA.alias(MemoryLocation::get(SI_int), MemoryLocation::get(SI), SimpleAAQI);
                                          if(AR_UDchain_noDep_SS != AliasResult::NoAlias){
                                            Aliases_with_prevStore = true; 
                                          }
                                        }
                                      }
                                      if(LoadInst *LI_int = dyn_cast<LoadInst>(noDepInst)){
                                        if(LI_int->comesBefore(SI)){
                                          AliasResult AR_UDchain_noDep_SL = AA.alias(MemoryLocation::get(LI_int), MemoryLocation::get(SI), SimpleAAQI);
                                          if(AR_UDchain_noDep_SL != AliasResult::NoAlias){
                                            Aliases_with_prevStore = true; 
                                          }
                                        }
                                      }
                                      if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                        assert(EnableAcrossCallReorderingStore_NN && "Across call reordering should be enabled");
                                        if(CI_int->comesBefore(SI)){
                                          ModRefInfo DMD_UDchain_noDep_SC = AA.getModRefInfo(CI_int, MemoryLocation::get(SI),  SimpleAAQI);
                                          ModRefInfo GMD_UDchain_noDep_SC = GlobalsAAR.getModRefInfo(CI_int, MemoryLocation::get(SI), SimpleAAQI);
                                          ModRefInfo OMD_UDchain_noDep_SC = (DMD_UDchain_noDep_SC == ModRefInfo::ModRef) ? GMD_UDchain_noDep_SC : DMD_UDchain_noDep_SC;
                                          if(OMD_UDchain_noDep_SC != ModRefInfo::NoModRef){
                                            Aliases_with_prevStore = true; 
                                          }
                                        }
                                      }
                                    }
                                }else if(SI != storeInst && !EnableUseDefChainCanContainStoreReorderingStore_NN){
                                  NumUDStoreWastedStore_NN++;
                                  Aliases_with_prevStore = true;
                                }
                              }

                              if(LoadInst *LI = dyn_cast<LoadInst>(I)){
                                AliasResult AR_prevStore_UD_load = AA.alias(MemoryLocation::get(prevStoreInst), MemoryLocation::get(LI), SimpleAAQI);
                                if(AR_prevStore_UD_load != AliasResult::NoAlias){
                                  Aliases_with_prevStore = true;
                                }
                                for(Instruction *noDepInst : noDepChain){
                                  if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                    if(SI_int->comesBefore(LI)){
                                      AliasResult AR_UDchain_noDep_LS = AA.alias(MemoryLocation::get(SI_int), MemoryLocation::get(LI), SimpleAAQI);
                                      if(AR_UDchain_noDep_LS != AliasResult::NoAlias){
                                        Aliases_with_prevStore = true; 
                                      }
                                    }
                                  }
                                  if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                    assert(EnableAcrossCallReorderingStore_NN && "Across call reordering should be enabled");
                                    if(CI_int->comesBefore(LI)){
                                      ModRefInfo DMD_UDchain_noDep_LC = AA.getModRefInfo(CI_int, MemoryLocation::get(LI), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_LC = GlobalsAAR.getModRefInfo(CI_int, MemoryLocation::get(LI), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_LC = (DMD_UDchain_noDep_LC == ModRefInfo::ModRef) ? GMD_UDchain_noDep_LC : DMD_UDchain_noDep_LC;
                                      if(OMD_UDchain_noDep_LC != ModRefInfo::NoModRef){
                                        Aliases_with_prevStore = true; 
                                      }
                                    }
                                  }
                                }
                              }

                              if(CallBase *CI = dyn_cast<CallBase>(I)){
                                assert(EnableAcrossCallReorderingStore_NN && "Across call reordering should be enabled");
                                ModRefInfo DMD_UDchain_noDep_CS1 = AA.getModRefInfo(CI, MemoryLocation::get(prevStoreInst), SimpleAAQI);
                                ModRefInfo GMD_UDchain_noDep_CS1 = GlobalsAAR.getModRefInfo(CI, MemoryLocation::get(prevStoreInst), SimpleAAQI);
                                ModRefInfo OMD_UDchain_noDep_CS1 = (DMD_UDchain_noDep_CS1 == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CS1 : DMD_UDchain_noDep_CS1;
                                if(OMD_UDchain_noDep_CS1 != ModRefInfo::NoModRef){
                                  Aliases_with_prevStore = true;
                                }
                                for(Instruction *noDepInst : noDepChain){
                                  if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                    if(SI_int->comesBefore(CI)){
                                      ModRefInfo DMD_UDchain_noDep_SC2 = AA.getModRefInfo(CI, MemoryLocation::get(SI_int), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_SC2 = GlobalsAAR.getModRefInfo(CI, MemoryLocation::get(SI_int), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_SC2 = (DMD_UDchain_noDep_SC2 == ModRefInfo::ModRef) ? GMD_UDchain_noDep_SC2 : DMD_UDchain_noDep_SC2;
                                      if(OMD_UDchain_noDep_SC2 != ModRefInfo::NoModRef){
                                        Aliases_with_prevStore = true; 
                                      }
                                    }
                                  }
                                  if(LoadInst *LI_int = dyn_cast<LoadInst>(noDepInst)){
                                    if(LI_int->comesBefore(CI)){
                                      ModRefInfo DMD_UDchain_noDep_CL = AA.getModRefInfo(CI, MemoryLocation::get(LI_int), SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_CL = GlobalsAAR.getModRefInfo(CI, MemoryLocation::get(LI_int), SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_CL = (DMD_UDchain_noDep_CL == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CL : DMD_UDchain_noDep_CL;
                                      if(OMD_UDchain_noDep_CL != ModRefInfo::NoModRef){
                                        Aliases_with_prevStore = true; 
                                      }
                                    }
                                  }
                                  if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                    assert(EnableAcrossCallReorderingStore_NN && "Across call reordering should be enabled");
                                    if(CI_int->comesBefore(CI)){
                                      ModRefInfo DMD_UDchain_noDep_CC = AA.getModRefInfo(CI, CI_int, SimpleAAQI);
                                      ModRefInfo GMD_UDchain_noDep_CC = GlobalsAAR.getModRefInfo(CI, CI_int, SimpleAAQI);
                                      ModRefInfo OMD_UDchain_noDep_CC = (DMD_UDchain_noDep_CC == ModRefInfo::ModRef) ? GMD_UDchain_noDep_CC : DMD_UDchain_noDep_CC;
                                      if(OMD_UDchain_noDep_CC != ModRefInfo::NoModRef){
                                        Aliases_with_prevStore = true; 
                                      }
                                    }
                                  }
                                }
                              }
                            }
                            for(Instruction *noDepInst : noDepChain){
                              assert((noDepInst->mayThrow() || !noDepInst->willReturn() || sideaffectcheck(noDepInst) || noDepInst->isVolatile() || noDepInst->isAtomic()) == false && "No-dep chain should not contain instructions with side effects");
                              if(StoreInst *SI_int = dyn_cast<StoreInst>(noDepInst)){
                                if(SI_int->comesBefore(storeInst) && SI_int != prevStoreInst){
                                  AliasResult AR_noDep_tailStore_S = AA.alias(MemoryLocation::get(SI_int), MemoryLocation::get(storeInst), SimpleAAQI);
                                  if(AR_noDep_tailStore_S != AliasResult::NoAlias){
                                    Aliases_with_prevStore = true; 
                                  }
                                }
                              }
                              if(LoadInst *LI_int = dyn_cast<LoadInst>(noDepInst)){
                                if(LI_int->comesBefore(storeInst)){
                                  AliasResult AR_noDep_tailStore_L = AA.alias(MemoryLocation::get(LI_int), MemoryLocation::get(storeInst), SimpleAAQI);
                                  if(AR_noDep_tailStore_L != AliasResult::NoAlias){
                                    Aliases_with_prevStore = true; 
                                  }
                                }
                              }
                              if(CallBase *CI_int = dyn_cast<CallBase>(noDepInst)){
                                assert(EnableAcrossCallReorderingStore_NN && "Across call reordering should be enabled");
                                if(CI_int->comesBefore(storeInst)){
                                  ModRefInfo DMD_noDep_tailStore_C = AA.getModRefInfo(CI_int, MemoryLocation::get(storeInst), SimpleAAQI);
                                  ModRefInfo GMD_noDep_tailStore_C = GlobalsAAR.getModRefInfo(CI_int, MemoryLocation::get(storeInst), SimpleAAQI);
                                  ModRefInfo OMD_noDep_tailStore_C = (DMD_noDep_tailStore_C == ModRefInfo::ModRef) ? GMD_noDep_tailStore_C : DMD_noDep_tailStore_C;
                                  if(OMD_noDep_tailStore_C != ModRefInfo::NoModRef){
                                    Aliases_with_prevStore = true; 
                                  }
                                }
                              }
                            }
                          }
                          #ifdef DEBUG_PRINT
                            if(UseChain_contains_prevStore){
                              errs() << "PrevStore in the UseChain: " << "True" << "\n";
                            }else{
                              errs() << "PrevStore in the UseChain: " << "False" << "\n";
                            }

                            if(UseChain_contains_currStore){
                              errs() << "curStore in the Prev Store UseChain: " << "True" << "\n";
                            }else{
                              errs() << "curStore in the Prev Store UseChain: " << "False" << "\n";
                            }
                          #endif
                        }
                    }
                    #ifdef DEBUG_DISTANCE
                      errs() << "Distance" << (distance -2) << "\n";
                    #endif
                    }
                    if(Aliases_with_prevStore || UseChain_contains_prevStore){
                      if(NumAliasingFailsStore_NN){
                        NumAliasingFailsStore_NN++;
                      }
                      #ifdef DEBUG_PRINT
                      errs() << "Tail store: " << *storeInst << "\n";
                      errs() << "Head store: " << *prevStoreInst << "\n";
                      errs() << "Cannot reorder due to: " << Aliases_with_prevStore << " Does UseChain contain previous store: " << UseChain_contains_prevStore << "\n";
                      for(auto it = Int_ins.rbegin(); it != Int_ins.rend(); ++it){
                        //print use chain and alias instructions
                        errs() << "Inst in between: " << *(*it) << "\n";
                      }
                      errs() << "Current store is in the use chain of previous store but previous store is not in the use chain of current store, cannot reorder" << "\n";
                      #endif
                      currentInst = nullptr;
                      storeInst = nullptr;
                      distance = 0;
                      defChain.clear();
                      Int_ins.clear();
                      useChain.clear();
                      useChain_ordered.clear();
                      noDepChain.clear();
                      prev_store_usechain.clear();
                      Aliases_with_prevStore = false;
                      UseChain_contains_prevStore = false;
                      NumWastedReordersStore_NN++;
                      continue;
                    }
                    for (auto it_int = Int_ins.rbegin(); it_int != Int_ins.rend(); ++it_int){
                      if(Instruction *LI = dyn_cast<StoreInst>(*it_int)){
                          if(LI == storeInst){
                            #ifdef DEBUG_PRINT
                            errs() << "Use Chain for tail store: " << *storeInst << "\n";
                            for (auto it = useChain.rbegin(); it != useChain.rend(); ++it) {//for (Instruction *inst : useChain) {
                              Instruction *inst = *it;
                              errs() << "Inst in Use Chain for tail store: " << *inst << "\n";
                            }
                            #endif
                            for (auto it = useChain_ordered.begin(); it != useChain_ordered.end(); ++it) {
                              Instruction *inst = *it;
                            //for (Instruction *inst : useChain) {
                              if(inst != prevStoreInst && inst == storeInst){
                                successfully_reordered = true;
                                reorder++;
                                inst->moveAfter(prevStoreInst);
                                #ifdef DEBUG_PRINT
                                errs() << "Inst move after head Store: " << *inst << "\n";
                                #endif
                              } else if(inst != prevStoreInst && inst != storeInst){
                                inst->moveBefore(prevStoreInst);
                                #ifdef DEBUG_PRINT
                                errs() << "Inst move before head Store: " << *inst << "\n";
                                #endif
                              }  
                            } 
                          }
                          #ifdef DEBUG_PRINT
                          if(LI == storeInst){
                            errs() << "User Chain for head Store: " << *prevStoreInst << "\n";
                            for (auto it = prev_store_usechain.rbegin(); it != prev_store_usechain.rend(); ++it) {//for (Instruction *inst : useChain) {
                              Instruction *inst = *it;
                              errs() << "Inst in head Store Use Chain: " << *inst << "\n";
                            }
                          }
                          #endif
                        }
                      }
                }
              if(successfully_reordered){
                // it_bb = ++storeInst->getIterator();
                it_bb = storeInst->getIterator();
                prevStoreInst = nullptr;
                call_inbetween = false; //comment
                successfully_reordered = false;
                currentInst = nullptr;
                storeInst = nullptr;
                distance = 0;
                globalReorder_store_NN++;
                run_globalReorderStore_NN++;
                NumSuccessfulReordersStore_NN++;
                // run_globalAcrossCallReorderStore_NN++;
              }else{
                currentInst = nullptr;
                storeInst = nullptr;
                // NumWastedReordersStore_NN++;
                // if(!Aliases_with_currStore)
                //   it_bb++;
              }
              #ifdef DEBUG_PRINT
              errs() << "--------" << "\n";
              errs() << "Resetting for next Store" << "\n";
              errs() << "--------" << "\n";
              errs() << "\n";
              #endif
              defChain.clear();
              Int_ins.clear();
              Int_dep.clear();
              useChain.clear();
              useChain_ordered.clear();
              call_inbetween = false; //comment
              prev_store_usechain.clear();
              noDepChain.clear();
              UseChain_contains_prevStore = false;
              Aliases_with_prevStore = false;
              Aliases_with_currStore = false;
              std::queue<Instruction *>().swap(worklist);
            }
            // if(successfully_reordered){
            //     prevStoreInst = nullptr;
            //     currentInst = nullptr;
            //     storeInst = nullptr;
            //     distance = 0;
            //   }else{
            //     prevStoreInst = storeInst;
            //   }
            if(!prevStoreInst){
              prevStoreInst = storeInst;
            }
          }
          else if(CallInst *callinst = dyn_cast<CallInst>(&I)){
            if(!storeInst && prevStoreInst){
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
      errs() << "Number of global wasted: " << globalWasted_store_NN << "\n";
      errs() << "Number of successful reorders: " << globalReorder_store_NN << "\n";
      #endif
    return PreservedAnalyses::all();
 }

PreservedAnalyses ReorderPassStore_NN::run(Module &M, ModuleAnalysisManager &AM) {
    // Initialize the analysis manager
    for(auto &F : M) {
      if (F.isDeclaration())
        continue;
      FunctionAnalysisManager &FAM =
          AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    // Run the reordering pass
      this->runonFunction(F, AM, FAM);
    }
    errs() << "store_run_intra_aa_svf_diffStore_NN: " << run_intra_aa_svf_diffStore_NN << "\n";
    errs() << "store_run_globalWastedStore_NN: " << run_globalWastedStore_NN << "\n";
    errs() << "store_run_globalAcrossCallReorderStore_NN: " << run_globalAcrossCallReorderStore_NN << "\n";
    errs() << "store_run_globalReorderStore_NN: " << run_globalReorderStore_NN << "\n";
    errs() << "store_run_globalWasterReorderStore_NN: " << run_globalWasterReorderStore_NN << "\n";
    // Return preserved analyses
    return PreservedAnalyses::all();
}