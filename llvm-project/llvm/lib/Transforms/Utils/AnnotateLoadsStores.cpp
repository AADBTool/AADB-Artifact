//===-- MarkROI.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/AnnotateLoadsStores.h"
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

// PreservedAnalyses AnnotateLoadsStoresPass::run(Module &M,
//                                       ModuleAnalysisManager &MAM) {
//     unsigned MemId = 0; // Initialize MemId to 0
//     int MemId_F = 0;
//     for (Function &F : M) {
//         for (BasicBlock &BB : F) {
//             for (Instruction &I : BB) {
//                 if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
//                     LLVMContext &Ctx = I.getContext();
//                     auto *MD = MDNode::get(Ctx, {MDString::get(Ctx, "mem_id" + std::to_string(MemId_F)),ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), MemId_F))});
//                     I.setMetadata("mem_id" + std::to_string(MemId_F), MD);
//                     // errs() << "Assigned mem_id = " << MemId_F << " to: " << I << "\n";

//                     MemId_F++; // increment after assigning and logging
//                 }
//             }
//         }
//     }
//   return PreservedAnalyses::all();
// }

PreservedAnalyses AnnotateLoadsStoresPass::run(Module &M,
                                      ModuleAnalysisManager &MAM) {
    unsigned MemId = 0; // Initialize MemId to 0
    int MemId_F = 0;
    int MemId_F2 = 1; // Start from 1 for stores to differentiate from loads
    for (Function &F : M) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (isa<LoadInst>(&I)) {
                    LLVMContext &Ctx = I.getContext();
                    auto *MD = MDNode::get(Ctx, {MDString::get(Ctx, "mem_id" + std::to_string(MemId_F)),ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), MemId_F))});
                    I.setMetadata("mem_id" + std::to_string(MemId_F), MD);
                    // errs() << "Assigned mem_id = " << MemId_F << " to: " << I << "\n";

                    MemId_F= MemId_F + 2; // increment after assigning and logging
                }
            }
        }
    }
    for (Function &F : M) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (isa<StoreInst>(&I)) {
                    LLVMContext &Ctx = I.getContext();
                    auto *MD = MDNode::get(Ctx, {MDString::get(Ctx, "mem_id" + std::to_string(MemId_F2)),ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), MemId_F2))});
                    I.setMetadata("mem_id" + std::to_string(MemId_F2), MD);
                    // errs() << "Assigned mem_id = " << MemId_F2 << " to: " << I << "\n";

                    MemId_F2= MemId_F2 + 2; // increment after assigning and logging
                }
            }
        }
    }
  return PreservedAnalyses::all();
}