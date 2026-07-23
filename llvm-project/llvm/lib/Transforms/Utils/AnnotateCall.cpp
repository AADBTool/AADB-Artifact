//===-- MarkROI.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/AnnotateCall.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InlineAsm.h"

using namespace llvm;
using namespace std;

PreservedAnalyses AnnotateCallsPass::run(Module &M,
                                      ModuleAnalysisManager &MAM) {
    unsigned CallId = 0; // Initialize CallId to 0
    int CallId_F = 0;
    for (Function &F : M) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (isa<CallInst>(&I)) {
                    LLVMContext &Ctx = I.getContext();
                    auto *MD = MDNode::get(Ctx, {MDString::get(Ctx, "call_id" + std::to_string(CallId)),ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), CallId))});
                    I.setMetadata("call_id" + std::to_string(CallId), MD);
                    // errs() << "Assigned call_id = " << CallId << " to: " << I << "\n";

                    CallId++; // increment after assigning and logging
                }
            }
        }
    }
  return PreservedAnalyses::all();
}