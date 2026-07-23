# How to Add an External Alias Analysis to LLVM

This guide explains how to add a custom alias analysis (AA) implementation to LLVM, covering both the legacy pass manager and the new pass manager approaches.

## Overview

LLVM provides multiple ways to integrate custom alias analysis:

1. **New Pass Manager (NPM)**: Modern approach using `AAManager` with function or module analyses
2. **Legacy Pass Manager**: Using `ExternalAAWrapperPass` for injection
3. **Target-Specific AA**: Integrated into target machine pass pipelines

## Architecture

### Key Components

- **`AAResultBase`**: Base class that AA implementations derive from
- **`AAResults`**: Aggregates multiple AA results and provides unified query interface
- **`AAManager`**: Manages AA registration in new pass manager
- **`ExternalAAWrapperPass`**: Legacy mechanism for injecting external AAs
- **Type Erasure**: `AAResults::Concept` and `AAResults::Model<T>` enable heterogeneous AA storage

## Implementation Steps

### Step 1: Create Your AA Result Class

Your AA must derive from `AAResultBase` and implement the query methods:

```cpp
// MyAliasAnalysis.h
#include "llvm/Analysis/AliasAnalysis.h"

namespace llvm {

class MyAAResult : public AAResultBase {
public:
  MyAAResult() {}
  MyAAResult(MyAAResult &&Arg) : AAResultBase(std::move(Arg)) {}

  // Handle invalidation for new pass manager
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv) {
    // Return false if your AA is stateless or doesn't need invalidation
    // Return true if you want to invalidate based on other analysis results
    return false;
  }

  // Main alias query interface
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI = nullptr);

  // Mod/ref mask for memory locations
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals);

  // Optional: Override other methods from AAResultBase as needed
  // - getArgModRefInfo
  // - getMemoryEffects (for calls/functions)
  // - getModRefInfo (for calls)
};

} // end namespace llvm
```

**Key Methods to Implement:**

- **`alias()`**: Returns whether two memory locations alias (NoAlias, MayAlias, PartialAlias, MustAlias)
- **`getModRefInfoMask()`**: Returns what operations (Mod/Ref) are possible on a location
- **`invalidate()`**: Determines if the result should be recomputed in NPM

**Important**: Any method you don't override inherits the conservative default from `AAResultBase` (e.g., `MayAlias`, `ModRef`).

### Step 2A: New Pass Manager Integration (Function-Level AA)

Create an analysis pass that returns your AA result:

```cpp
// MyAliasAnalysis.h (continued)

/// Analysis pass for the new pass manager
class MyAA : public AnalysisInfoMixin<MyAA> {
  friend AnalysisInfoMixin<MyAA>;
  static AnalysisKey Key;

public:
  using Result = MyAAResult;

  MyAAResult run(Function &F, FunctionAnalysisManager &AM) {
    return MyAAResult();
  }
};

} // end namespace llvm
```

```cpp
// MyAliasAnalysis.cpp
#include "MyAliasAnalysis.h"

using namespace llvm;

AnalysisKey MyAA::Key;

AliasResult MyAAResult::alias(const MemoryLocation &LocA,
                              const MemoryLocation &LocB,
                              AAQueryInfo &AAQI,
                              const Instruction *CtxI) {
  // Your alias analysis logic here
  // Return NoAlias if you can prove no aliasing
  // Return MustAlias if they definitely alias
  // Return MayAlias if unsure (conservative default)
  return AliasResult::MayAlias;
}

ModRefInfo MyAAResult::getModRefInfoMask(const MemoryLocation &Loc,
                                         AAQueryInfo &AAQI,
                                         bool IgnoreLocals) {
  // Return NoModRef if location is read-only or inaccessible
  // Return ModRefInfo::Mod if only writes possible
  // Return ModRefInfo::Ref if only reads possible
  // Return ModRefInfo::ModRef if both or unsure (conservative)
  return ModRefInfo::ModRef;
}
```

**Register with PassBuilder:**

```cpp
// In your target's initialization or PassBuilder callback
void registerMyAAWithPipeline(PassBuilder &PB) {
  // Register function analysis
  PB.registerFunctionAnalysisRegis trationCallback(
      [](FunctionAnalysisManager &FAM) {
        FAM.registerPass([&] { return MyAA(); });
      });

  // Register with AAManager for AA pipeline
  PB.registerAAParsingCallback(
      [](StringRef Name, AAManager &AA) {
        if (Name == "my-aa") {
          AA.registerFunctionAnalysis<MyAA>();
          return true;
        }
        return false;
      });
}
```

**Enable in Pass Pipeline:**

Add to your AA pipeline string:
```
-passes='require<aa-manager>,function(my-aa)'
```

Or modify `AAManager` in code:
```cpp
AAManager AA;
AA.registerFunctionAnalysis<MyAA>();
```

### Step 2B: New Pass Manager Integration (Module-Level AA)

For module-level analysis (e.g., inter-procedural AA):

```cpp
class MyModuleAA : public AnalysisInfoMixin<MyModuleAA> {
  friend AnalysisInfoMixin<MyModuleAA>;
  static AnalysisKey Key;

public:
  using Result = MyAAResult;

  MyAAResult run(Module &M, ModuleAnalysisManager &MAM) {
    // Module-level analysis initialization
    return MyAAResult();
  }
};
```

Register as module analysis:

```cpp
PB.registerModuleAnalysisRegistrationCallback(
    [](ModuleAnalysisManager &MAM) {
      MAM.registerPass([&] { return MyModuleAA(); });
    });

PB.registerAAParsingCallback(
    [](StringRef Name, AAManager &AA) {
      if (Name == "my-module-aa") {
        AA.registerModuleAnalysis<MyModuleAA>();
        return true;
      }
      return false;
    });
```

### Step 3: Legacy Pass Manager Integration

For compatibility with legacy pass manager:

```cpp
// MyAliasAnalysis.h (continued)

/// Legacy wrapper pass
class MyAAWrapperPass : public ImmutablePass {
  std::unique_ptr<MyAAResult> Result;

public:
  static char ID;

  MyAAWrapperPass();

  MyAAResult &getResult() { return *Result; }
  const MyAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override {
    Result.reset(new MyAAResult());
    return false;
  }

  bool doFinalization(Module &M) override {
    Result.reset();
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// External AA wrapper for easy injection
class MyExternalAAWrapper : public ExternalAAWrapperPass {
public:
  static char ID;

  MyExternalAAWrapper()
      : ExternalAAWrapperPass([](Pass &P, Function &, AAResults &AAR) {
          if (auto *WrapperPass = P.getAnalysisIfAvailable<MyAAWrapperPass>())
            AAR.addAAResult(WrapperPass->getResult());
        }) {}
};
```

```cpp
// MyAliasAnalysis.cpp (continued)

char MyAAWrapperPass::ID = 0;
char MyExternalAAWrapper::ID = 0;

INITIALIZE_PASS(MyAAWrapperPass, "my-aa",
                "My Custom Alias Analysis", false, true)

INITIALIZE_PASS(MyExternalAAWrapper, "my-aa-wrapper",
                "My Custom AA External Wrapper", false, true)

MyAAWrapperPass::MyAAWrapperPass() : ImmutablePass(ID) {
  initializeMyAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void MyAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

ImmutablePass *llvm::createMyAAWrapperPass() {
  return new MyAAWrapperPass();
}

ImmutablePass *llvm::createMyExternalAAWrapperPass() {
  return new MyExternalAAWrapper();
}
```

**Add to Legacy Pass Pipeline:**

```cpp
// In your target machine's pass configuration
void MyTargetPassConfig::addIRPasses() {
  TargetPassConfig::addIRPasses();
  
  // Add the wrapper pass
  addPass(createMyAAWrapperPass());
  
  // Inject into AAResults via ExternalAAWrapperPass
  addPass(createExternalAAWrapperPass([](Pass &P, Function &, AAResults &AAR) {
    if (auto *WrapperPass = P.getAnalysisIfAvailable<MyAAWrapperPass>())
      AAR.addAAResult(WrapperPass->getResult());
  }));
}
```

## Complete Example: NVPTX Target

Let's look at how NVPTX implements address-space-based AA:

### Header (NVPTXAliasAnalysis.h):

```cpp
class NVPTXAAResult : public AAResultBase {
public:
  NVPTXAAResult() {}
  NVPTXAAResult(NVPTXAAResult &&Arg) : AAResultBase(std::move(Arg)) {}

  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;  // Stateless, never invalidates
  }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI = nullptr);
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals);
};

// New pass manager analysis
class NVPTXAA : public AnalysisInfoMixin<NVPTXAA> {
  friend AnalysisInfoMixin<NVPTXAA>;
  static AnalysisKey Key;

public:
  using Result = NVPTXAAResult;
  NVPTXAAResult run(Function &F, AnalysisManager<Function> &AM) {
    return NVPTXAAResult();
  }
};

// Legacy wrapper
class NVPTXAAWrapperPass : public ImmutablePass {
  std::unique_ptr<NVPTXAAResult> Result;
public:
  static char ID;
  NVPTXAAWrapperPass();
  NVPTXAAResult &getResult() { return *Result; }
  // ... doInitialization, doFinalization, getAnalysisUsage
};

// External AA injector
class NVPTXExternalAAWrapper : public ExternalAAWrapperPass {
public:
  static char ID;
  NVPTXExternalAAWrapper()
      : ExternalAAWrapperPass([](Pass &P, Function &, AAResults &AAR) {
          if (auto *WrapperPass = P.getAnalysisIfAvailable<NVPTXAAWrapperPass>())
            AAR.addAAResult(WrapperPass->getResult());
        }) {}
};
```

### Implementation (NVPTXAliasAnalysis.cpp):

```cpp
AliasResult NVPTXAAResult::alias(const MemoryLocation &Loc1,
                                 const MemoryLocation &Loc2,
                                 AAQueryInfo &AAQI,
                                 const Instruction *) {
  unsigned AS1 = Loc1.Ptr->getType()->getPointerAddressSpace();
  unsigned AS2 = Loc2.Ptr->getType()->getPointerAddressSpace();

  // Different address spaces cannot alias (except for generic)
  if ((AS1 == ADDRESS_SPACE_GENERIC) || (AS2 == ADDRESS_SPACE_GENERIC))
    return AliasResult::MayAlias;

  return (AS1 == AS2 ? AliasResult::MayAlias : AliasResult::NoAlias);
}

ModRefInfo NVPTXAAResult::getModRefInfoMask(const MemoryLocation &Loc,
                                            AAQueryInfo &AAQI,
                                            bool IgnoreLocals) {
  // Constant and parameter address spaces are read-only
  if (isConstOrParam(Loc.Ptr->getType()->getPointerAddressSpace()))
    return ModRefInfo::NoModRef;

  return ModRefInfo::ModRef;
}
```

### Target Machine Integration (NVPTXTargetMachine.cpp):

```cpp
void NVPTXPassConfig::addIRPasses() {
  // ... other passes
  
  addPass(createNVPTXAAWrapperPass());
  addPass(createExternalAAWrapperPass([](Pass &P, Function &, AAResults &AAR) {
    if (auto *WrapperPass = P.getAnalysisIfAvailable<NVPTXAAWrapperPass>())
      AAR.addAAResult(WrapperPass->getResult());
  }));
  
  // ... more passes
}
```

## Best Practices

### 1. Return Conservative Results

When unsure, return the safe conservative answer:
- **Alias queries**: Return `MayAlias` (assume they might alias)
- **Mod/Ref queries**: Return `ModRefInfo::ModRef` (assume both read and write)

### 2. Call Base Class for Unknown Cases

```cpp
AliasResult MyAAResult::alias(const MemoryLocation &LocA,
                              const MemoryLocation &LocB,
                              AAQueryInfo &AAQI,
                              const Instruction *CtxI) {
  // Your custom logic
  if (canProveNoAlias(LocA, LocB))
    return AliasResult::NoAlias;
  
  // Fall back to conservative default
  return AAResultBase::alias(LocA, LocB, AAQI, CtxI);
}
```

### 3. Maintain Statelessness When Possible

Stateless AAs (like NVPTX's address-space-based AA) never need invalidation:

```cpp
bool invalidate(Function &, const PreservedAnalyses &,
                FunctionAnalysisManager::Invalidator &) {
  return false;  // Never invalidate
}
```

### 4. Order Matters in AAManager

AAs are queried in registration order. Register more precise analyses first:

```cpp
AAManager AA;
AA.registerFunctionAnalysis<MyPreciseAA>();    // Checked first
AA.registerFunctionAnalysis<BasicAA>();         // Fallback
```

### 5. Use AAQueryInfo for Recursion

`AAQueryInfo` tracks recursion depth and caches results:

```cpp
AliasResult MyAAResult::alias(const MemoryLocation &LocA,
                              const MemoryLocation &LocB,
                              AAQueryInfo &AAQI,
                              const Instruction *CtxI) {
  // Use AAQI to query other AAs or make recursive queries
  return AAQI.AAR.alias(UnderlyingA, UnderlyingB, AAQI, CtxI);
}
```

### 6. Test Thoroughly

Test your AA with:
- **Unit tests**: Direct AA queries
- **Opt tests**: Check optimization impact (`-aa-eval`, `-licm`, `-gvn`, etc.)
- **Integration tests**: Ensure correctness with real programs

## Common Patterns

### Address-Space-Based AA

Useful for targets with distinct address spaces:

```cpp
AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                  AAQueryInfo &AAQI, const Instruction *) {
  unsigned AS1 = LocA.Ptr->getType()->getPointerAddressSpace();
  unsigned AS2 = LocB.Ptr->getType()->getPointerAddressSpace();
  
  if (AS1 != AS2)
    return AliasResult::NoAlias;
  
  return AliasResult::MayAlias;
}
```

### Type-Based AA

Leverage type metadata:

```cpp
AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                  AAQueryInfo &AAQI, const Instruction *) {
  // Check TBAA metadata
  if (LocA.AATags.TBAA && LocB.AATags.TBAA) {
    if (noAliasPerTBAA(LocA.AATags.TBAA, LocB.AATags.TBAA))
      return AliasResult::NoAlias;
  }
  
  return AliasResult::MayAlias;
}
```

### Constant Memory

Mark read-only regions:

```cpp
ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                             AAQueryInfo &AAQI,
                             bool IgnoreLocals) {
  if (isConstantMemory(Loc.Ptr))
    return ModRefInfo::Ref;  // Read-only
  
  return ModRefInfo::ModRef;
}
```

## Debugging Tips

### Enable AA Debugging

```bash
# Print AA queries
opt -debug-only=aa -your-pass input.ll

# Evaluate AA precision
opt -aa-eval -print-all-alias-modref-info input.ll
```

### Common Issues

1. **Too aggressive (unsound)**: Returning `NoAlias` when aliasing is possible
   - **Fix**: Be conservative, only return `NoAlias` when certain

2. **Too conservative**: Always returning `MayAlias`
   - **Fix**: Implement more precise logic for your specific cases

3. **Invalidation problems**: Results not updated after IR changes
   - **Fix**: Implement proper `invalidate()` logic or maintain statelessness

4. **Registration order**: Your AA not being queried
   - **Fix**: Register before less precise AAs (e.g., before BasicAA)

## Registration Checklist

For complete integration, ensure:

- [ ] AA result class derives from `AAResultBase`
- [ ] New pass manager analysis class defined (with `AnalysisKey`)
- [ ] Legacy wrapper pass created (for legacy compatibility)
- [ ] External AA wrapper implemented (for easy injection)
- [ ] Passes registered with `INITIALIZE_PASS`
- [ ] Factory functions created (`createMyAAWrapperPass()`)
- [ ] Registered with `AAManager` in new pass manager
- [ ] Added to target machine pass pipeline
- [ ] Tests written for functionality and precision

## Further Reading

- [LLVM Alias Analysis Documentation](https://llvm.org/docs/AliasAnalysis.html)
- [LLVM Pass Manager Documentation](https://llvm.org/docs/WritingAnLLVMPass.html)
- [New Pass Manager Documentation](https://llvm.org/docs/NewPassManager.html)
- Example implementations:
  - [BasicAA](../lib/Analysis/BasicAliasAnalysis.cpp)
  - [TBAA](../lib/Analysis/TypeBasedAliasAnalysis.cpp)
  - [NVPTX AA](../lib/Target/NVPTX/NVPTXAliasAnalysis.cpp)
  - [AMDGPU AA](../lib/Target/AMDGPU/AMDGPUAliasAnalysis.cpp)
  - [GlobalsAA](../lib/Analysis/GlobalsModRef.cpp)

## Summary

Adding an external AA to LLVM involves:

1. **Implement** your AA result class deriving from `AAResultBase`
2. **Create** new pass manager analysis (function or module level)
3. **Add** legacy wrapper for compatibility
4. **Register** with `AAManager` and in target pipeline
5. **Test** for correctness and precision

The key is understanding the type-erased aggregation in `AAResults` and the registration mechanisms in both pass managers. Start with simple cases (like address-space analysis) and expand as needed.
