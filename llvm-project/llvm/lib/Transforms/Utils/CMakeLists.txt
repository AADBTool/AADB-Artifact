add_llvm_component_library(LLVMTransformUtils
  AddDiscriminators.cpp
  AliasGraphBuilder.cpp
  AliasTestPass.cpp 
  AMDGPUEmitPrintf.cpp
  ASanStackFrameLayout.cpp
  AssumeBundleBuilder.cpp
  BasicBlockUtils.cpp
  BreakCriticalEdges.cpp
  BuildLibCalls.cpp
  BypassSlowDivision.cpp
  CallPromotionUtils.cpp
  CallGraphUpdater.cpp
  CanonicalizeAliases.cpp
  CanonicalizeFreezeInLoops.cpp
  CloneFunction.cpp
  CloneModule.cpp
  CodeExtractor.cpp
  CodeLayout.cpp
  CodeMoverUtils.cpp
  CtorUtils.cpp
  CountVisits.cpp
  Debugify.cpp
  DemoteRegToStack.cpp
  DXILUpgrade.cpp
  EntryExitInstrumenter.cpp
  EscapeEnumerator.cpp
  Evaluator.cpp
  FixIrreducible.cpp
  FlattenCFG.cpp
  FunctionComparator.cpp
  FunctionImportUtils.cpp
  GlobalStatus.cpp
  GuardUtils.cpp
  HelloWorld.cpp
  AliasTestPass.cpp
  PAAMWorld.cpp
  MarkROI.cpp
  Reorder.cpp
  Reorder_NN.cpp
  Reorder_Spec.cpp
  Reorderstore_NN.cpp
  AnnotateLoadsStores.cpp
  AnnotateLoadsStoreRe.cpp
  AnnotateCall.cpp
  AnnotateCallRe.cpp
  ReorderSpeculative.cpp
  Reorderstore.cpp
  UnrollInnerMostLoop.cpp
  InlineFunction.cpp
  InjectTLIMappings.cpp
  InstructionNamer.cpp
  IntegerDivision.cpp
  LCSSA.cpp
  LibCallsShrinkWrap.cpp
  Local.cpp
  LoopConstrainer.cpp
  LoopPeel.cpp
  LoopRotationUtils.cpp
  LoopSimplify.cpp
  LoopUnroll.cpp
  LoopUnrollAndJam.cpp
  LoopUnrollRuntime.cpp
  LoopUtils.cpp
  LoopVersioning.cpp
  LowerAtomic.cpp
  LowerGlobalDtors.cpp
  LowerIFunc.cpp
  LowerInvoke.cpp
  LowerMemIntrinsics.cpp
  LowerSwitch.cpp
  MatrixUtils.cpp
  MemoryOpRemark.cpp
  MemoryTaggingSupport.cpp
  Mem2Reg.cpp
  MetaRenamer.cpp
  MisExpect.cpp
  ModuleUtils.cpp
  MoveAutoInit.cpp
  NameAnonGlobals.cpp
  PredicateInfo.cpp
  PromoteMemoryToRegister.cpp
  RelLookupTableConverter.cpp
  ScalarEvolutionExpander.cpp
  SCCPSolver.cpp
  StripGCRelocates.cpp
  SSAUpdater.cpp
  SSAUpdaterBulk.cpp
  SampleProfileInference.cpp
  SampleProfileLoaderBaseUtil.cpp
  SanitizerStats.cpp
  SimplifyCFG.cpp
  SimplifyIndVar.cpp
  SimplifyLibCalls.cpp
  SizeOpts.cpp
  SplitModule.cpp
  StripNonLineTableDebugInfo.cpp
  SymbolRewriter.cpp
  UnifyFunctionExitNodes.cpp
  UnifyLoopExits.cpp
  Utils.cpp
  ValueMapper.cpp
  VNCoercion.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Utils

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TargetParser
  )
  find_package(OpenMP REQUIRED)
  find_library(MYSQL_LIB mysqlclient REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql)
# Configure OpenMP - link to entire library but only compile flags for specific file
target_link_libraries(LLVMTransformUtils PRIVATE OpenMP::OpenMP_CXX)
# Set compile flags only for specific files
set_source_files_properties(Reorder_Spec.cpp PROPERTIES 
    COMPILE_FLAGS "-fopenmp -fexceptions"
    COMPILE_DEFINITIONS "SVF_ENABLE_OPENMP"
)
set_source_files_properties(Reorderstore_NN.cpp PROPERTIES 
    COMPILE_FLAGS "-fopenmp -fexceptions"
    COMPILE_DEFINITIONS "SVF_ENABLE_OPENMP"
)
target_include_directories(LLVMTransformUtils PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(LLVMTransformUtils PRIVATE ${MYSQL_LIB})
set_source_files_properties(Reorder_NN.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")