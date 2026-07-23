add_llvm_component_library(LLVMScalarOpts
  ADCE.cpp
  AlignmentFromAssumptions.cpp
  AnnotationRemarks.cpp
  BDCE.cpp
  CallSiteSplitting.cpp
  ConstantHoisting.cpp
  ConstraintElimination.cpp
  CorrelatedValuePropagation.cpp
  DCE.cpp
  DeadStoreElimination.cpp
  DFAJumpThreading.cpp
  DivRemPairs.cpp
  EarlyCSE.cpp
  FlattenCFGPass.cpp
  Float2Int.cpp
  GuardWidening.cpp
  GVN.cpp
  GVNHoist.cpp
  GVNSink.cpp
  IVUsersPrinter.cpp
  InductiveRangeCheckElimination.cpp
  IndVarSimplify.cpp
  InferAddressSpaces.cpp
  InferAlignment.cpp
  InstSimplifyPass.cpp
  JumpThreading.cpp
  LICM.cpp
  LoopAccessAnalysisPrinter.cpp
  LoopBoundSplit.cpp
  LoopSink.cpp
  LoopDeletion.cpp
  LoopDataPrefetch.cpp
  LoopDistribute.cpp
  LoopFuse.cpp
  LoopIdiomRecognize.cpp
  LoopInstSimplify.cpp
  LoopInterchange.cpp
  LoopFlatten.cpp
  LoopLoadElimination.cpp
  LoopPassManager.cpp
  LoopPredication.cpp
  LoopRerollPass.cpp
  LoopRotation.cpp
  LoopSimplifyCFG.cpp
  LoopStrengthReduce.cpp
  LoopUnrollPass.cpp
  LoopUnrollAndJamPass.cpp
  LoopVersioningLICM.cpp
  LowerAtomicPass.cpp
  LowerConstantIntrinsics.cpp
  LowerExpectIntrinsic.cpp
  LowerGuardIntrinsic.cpp
  LowerMatrixIntrinsics.cpp
  LowerWidenableCondition.cpp
  MakeGuardsExplicit.cpp
  MemCpyOptimizer.cpp
  MergeICmps.cpp
  MergedLoadStoreMotion.cpp
  NaryReassociate.cpp
  NewGVN.cpp
  PartiallyInlineLibCalls.cpp
  PlaceSafepoints.cpp
  Reassociate.cpp
  Reg2Mem.cpp
  RewriteStatepointsForGC.cpp
  SCCP.cpp
  SROA.cpp
  Scalar.cpp
  Scalarizer.cpp
  ScalarizeMaskedMemIntrin.cpp
  SeparateConstOffsetFromGEP.cpp
  SimpleLoopUnswitch.cpp
  SimplifyCFGPass.cpp
  Sink.cpp
  SpeculativeExecution.cpp
  StraightLineStrengthReduce.cpp
  StructurizeCFG.cpp
  TailRecursionElimination.cpp
  TLSVariableHoist.cpp
  WarnMissedTransforms.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Scalar

  DEPENDS
  intrinsics_gen

  COMPONENT_NAME
  Scalar

  LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  Core
  InstCombine
  Support
  TransformUtils
  )
  find_package(OpenMP REQUIRED)
  find_library(MYSQL_LIB mysqlclient REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql)
# Configure OpenMP - link to entire library but only compile flags for specific file
target_link_libraries(LLVMScalarOpts PRIVATE OpenMP::OpenMP_CXX)
# Set compile flags only for specific files
set_source_files_properties(LICM.cpp PROPERTIES 
    COMPILE_FLAGS "-fopenmp -fexceptions"
    COMPILE_DEFINITIONS "SVF_ENABLE_OPENMP"
)
target_include_directories(LLVMScalarOpts PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(LLVMScalarOpts PRIVATE ${MYSQL_LIB})
set_source_files_properties(LICM.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")
