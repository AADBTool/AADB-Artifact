//===- SVFsAA.h - Minimal SVFs AA interface ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Minimal, stateful alias analysis interface for SVFsAA.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SVFSAA_H
#define LLVM_ANALYSIS_SVFSAA_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include <functional>
#include <memory>
#include <map>
namespace llvm {

class CallGraph;
class DataLayout;
class Function;
class Module;
class TargetLibraryInfo;

/// Minimal SVFsAA result. This is stateful (non-stateless) by design.
class SVFsAAResult : public AAResultBase {
  const DataLayout &DL;
  const Function &F;
  const TargetLibraryInfo &TLI;

public:
  SVFsAAResult(
      const DataLayout &DL,
      const Function &F,
      const TargetLibraryInfo &TLI) : DL(DL), F(F), TLI(TLI) {};
    
      SVFsAAResult(const SVFsAAResult &Arg)
      : AAResultBase(Arg), DL(Arg.DL), F(Arg.F), TLI(Arg.TLI) {}
  SVFsAAResult(SVFsAAResult &&Arg)
      : AAResultBase(std::move(Arg)), DL(Arg.DL), F(Arg.F), TLI(Arg.TLI) {}

  /// Handle invalidation events in the new pass manager.
  bool invalidate(Function &Fn, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);
  const Module &getModule() { return *F.getParent(); }
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI);

  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);

  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI);

  /// Returns a bitmask that should be unconditionally applied to the ModRef
  /// info of a memory location. This allows us to eliminate Mod and/or Ref
  /// from the ModRef info based on the knowledge that the memory location
  /// points to constant and/or locally-invariant memory.
  ///
  /// If IgnoreLocals is true, then this method returns NoModRef for memory
  /// that points to a local alloca.
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals = false);

  /// Get the location associated with a pointer argument of a callsite.
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx);

  /// Returns the behavior when calling the given call site.
  MemoryEffects getMemoryEffects(const CallBase *Call, AAQueryInfo &AAQI);

  /// Returns the behavior when calling the given function. For use when the
  /// call site is not known.
  MemoryEffects getMemoryEffects(const Function *Fn);
  std::unordered_map<std::string, int> CacheSVF; // Cache for metadata lookups
  bool connectToDB = false; // Flag to control database connection first time
};

/// Analysis pass for SVFsAA.
class SVFsAA : public AnalysisInfoMixin<SVFsAA> {
  friend AnalysisInfoMixin<SVFsAA>;
  static AnalysisKey Key;

public:
  using Result = SVFsAAResult;

  SVFsAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the SVFsAAResult object.
class SVFsAAWrapperPass : public FunctionPass {
  std::unique_ptr<SVFsAAResult> Result;

  virtual void anchor();

public:
  static char ID;

  SVFsAAWrapperPass();

  SVFsAAResult &getResult() { return *Result; }
  const SVFsAAResult &getResult() const { return *Result; }

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createSVFsAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_SVFSAA_H
