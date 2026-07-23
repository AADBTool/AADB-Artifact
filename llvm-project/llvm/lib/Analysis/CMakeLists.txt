if (DEFINED LLVM_HAVE_TF_AOT OR LLVM_HAVE_TFLITE)
  include(TensorFlowCompile)
  set(LLVM_INLINER_MODEL_PATH_DEFAULT "models/inliner-Oz")

  set(LLVM_INLINER_MODEL_CURRENT_URL "<UNSPECIFIED>" CACHE STRING "URL to download the LLVM inliner model")

  if (DEFINED LLVM_HAVE_TF_AOT)
    tf_find_and_compile(
      ${LLVM_INLINER_MODEL_PATH}
      ${LLVM_INLINER_MODEL_CURRENT_URL}
      ${LLVM_INLINER_MODEL_PATH_DEFAULT}
      "models/gen-inline-oz-test-model.py"
      serve
      action
      InlinerSizeModel
      llvm::InlinerSizeModel
    )
  endif()

  if (LLVM_HAVE_TFLITE)
    list(APPEND MLLinkDeps
      tensorflow-lite::tensorflow-lite)
  endif()
endif()

add_llvm_component_library(LLVMAnalysis
  AliasAnalysis.cpp
  AliasAnalysisEvaluator.cpp
  AliasGraph.cpp
  AliasSetTracker.cpp
  Analysis.cpp
  AssumeBundleQueries.cpp
  AssumptionCache.cpp
  BasicAliasAnalysis.cpp
  BlockFrequencyInfo.cpp
  BlockFrequencyInfoImpl.cpp
  BranchProbabilityInfo.cpp
  CFG.cpp
  CFGPrinter.cpp
  CFGSCCPrinter.cpp
  CGSCCPassManager.cpp
  CallGraph.cpp
  CallGraphSCCPass.cpp
  CallPrinter.cpp
  CaptureTracking.cpp
  CmpInstAnalysis.cpp
  CostModel.cpp
  CodeMetrics.cpp
  ConstantFolding.cpp
  CycleAnalysis.cpp
  DDG.cpp
  DDGPrinter.cpp
  ConstraintSystem.cpp
  Delinearization.cpp
  DemandedBits.cpp
  DependenceAnalysis.cpp
  DependenceGraphBuilder.cpp
  DevelopmentModeInlineAdvisor.cpp
  DomConditionCache.cpp
  DomPrinter.cpp
  DomTreeUpdater.cpp
  DominanceFrontier.cpp
  FunctionPropertiesAnalysis.cpp
  GlobalsModRef.cpp
  GuardUtils.cpp
  HeatUtils.cpp
  IRSimilarityIdentifier.cpp
  IVDescriptors.cpp
  IVUsers.cpp
  ImportedFunctionsInliningStatistics.cpp
  IndirectCallPromotionAnalysis.cpp
  InlineCost.cpp
  InlineAdvisor.cpp
  InlineOrder.cpp
  InlineSizeEstimatorAnalysis.cpp
  InstCount.cpp
  InstructionPrecedenceTracking.cpp
  InstructionSimplify.cpp
  InteractiveModelRunner.cpp
  Interval.cpp
  IntervalPartition.cpp
  LazyBranchProbabilityInfo.cpp
  LazyBlockFrequencyInfo.cpp
  LazyCallGraph.cpp
  LazyValueInfo.cpp
  Lint.cpp
  Loads.cpp
  Local.cpp
  LoopAccessAnalysis.cpp
  LoopAnalysisManager.cpp
  LoopCacheAnalysis.cpp
  LoopNestAnalysis.cpp
  LoopUnrollAnalyzer.cpp
  LoopInfo.cpp
  LoopPass.cpp
  MLInlineAdvisor.cpp
  MemDerefPrinter.cpp
  MemoryBuiltins.cpp
  MemoryDependenceAnalysis.cpp
  MemoryLocation.cpp
  MemoryProfileInfo.cpp
  MemorySSA.cpp
  MemorySSAUpdater.cpp
  ModelUnderTrainingRunner.cpp
  ModuleDebugInfoPrinter.cpp
  ModuleSummaryAnalysis.cpp
  MustExecute.cpp
  NoInferenceModelRunner.cpp
  ObjCARCAliasAnalysis.cpp
  ObjCARCAnalysisUtils.cpp
  ObjCARCInstKind.cpp
  OptimizationRemarkEmitter.cpp
  OverflowInstAnalysis.cpp
  PHITransAddr.cpp
  PhiValues.cpp
  PostDominators.cpp
  ProfileSummaryInfo.cpp
  PtrUseVisitor.cpp
  RegionInfo.cpp
  RegionPass.cpp
  RegionPrinter.cpp
  ReplayInlineAdvisor.cpp
  ScalarEvolution.cpp
  ScalarEvolutionAliasAnalysis.cpp
  ScalarEvolutionDivision.cpp
  ScalarEvolutionNormalization.cpp
  StackLifetime.cpp
  StackSafetyAnalysis.cpp
  StructuralHash.cpp
  SVFsAA.cpp
  SyntheticCountsUtils.cpp
  TFLiteUtils.cpp
  TargetLibraryInfo.cpp
  TargetTransformInfo.cpp
  TensorSpec.cpp
  Trace.cpp
  TrainingLogger.cpp
  TypeBasedAliasAnalysis.cpp
  TypeMetadataUtils.cpp
  UniformityAnalysis.cpp
  ScopedNoAliasAA.cpp
  ValueLattice.cpp
  ValueLatticeUtils.cpp
  ValueTracking.cpp
  VectorUtils.cpp
  VFABIDemangling.cpp
  ${GeneratedMLSources}

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Analysis

  DEPENDS
  intrinsics_gen
  ${MLDeps}

  LINK_LIBS
  ${MLLinkDeps}

  LINK_COMPONENTS
  BinaryFormat
  Core
  Object
  ProfileData
  Support
  TargetParser
  )
  find_package(OpenMP REQUIRED)
  find_library(MYSQL_LIB mysqlclient REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql)
# Configure OpenMP - link to entire library but only compile flags for specific file
target_link_libraries(LLVMAnalysis PRIVATE OpenMP::OpenMP_CXX)
# Set compile flags only for specific files
set_source_files_properties(SVFsAA.cpp PROPERTIES 
    COMPILE_FLAGS "-fopenmp -fexceptions"
    COMPILE_DEFINITIONS "SVF_ENABLE_OPENMP"
)
# Find PostgreSQL (libpq)
find_package(PostgreSQL REQUIRED)

# Link PostgreSQL client library and expose headers for sources like WPAPass.cpp
target_link_libraries(LLVMAnalysis PUBLIC ${PostgreSQL_LIBRARIES})
target_include_directories(LLVMAnalysis PRIVATE ${PostgreSQL_INCLUDE_DIRS})

target_include_directories(LLVMAnalysis PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(LLVMAnalysis PRIVATE ${MYSQL_LIB})
set_source_files_properties(SVFsAA.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")
# Find fmt (for -lfmt)
find_library(FMT_LIBRARY fmt
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    DOC "fmt formatting library"
)

if(NOT FMT_LIBRARY)
    message(FATAL_ERROR "fmt library not found. Please install libfmt-dev")
endif()

# Link fmt to SvfCore
target_link_libraries(LLVMAnalysis PUBLIC ${FMT_LIBRARY})
