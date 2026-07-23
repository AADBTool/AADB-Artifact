add_llvm_component_library(LLVMVectorize
  LoadStoreVectorizer.cpp
  LoopVectorizationLegality.cpp
  LoopVectorize.cpp
  SLPVectorizer.cpp
  Vectorize.cpp
  VectorCombine.cpp
  VPlan.cpp
  VPlanAnalysis.cpp
  VPlanHCFGBuilder.cpp
  VPlanRecipes.cpp
  VPlanSLP.cpp
  VPlanTransforms.cpp
  VPlanVerifier.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Vectorize

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils
  )
  find_package(OpenMP REQUIRED)
  find_library(MYSQL_LIB mysqlclient REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql)
# Configure OpenMP - link to entire library but only compile flags for specific file
target_link_libraries(LLVMVectorize PRIVATE OpenMP::OpenMP_CXX)
# Set compile flags only for specific files
set_source_files_properties(SLPVectorizer.cpp PROPERTIES 
    COMPILE_FLAGS "-fopenmp -fexceptions"
    COMPILE_DEFINITIONS "SVF_ENABLE_OPENMP"
)
# Find PostgreSQL (libpq)
find_package(PostgreSQL REQUIRED)

# Link PostgreSQL client library and expose headers for sources like WPAPass.cpp
target_link_libraries(LLVMVectorize PUBLIC ${PostgreSQL_LIBRARIES})
target_include_directories(LLVMVectorize PRIVATE ${PostgreSQL_INCLUDE_DIRS})

target_include_directories(LLVMVectorize PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(LLVMVectorize PRIVATE ${MYSQL_LIB})
# Find fmt (for -lfmt)
find_library(FMT_LIBRARY fmt
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    DOC "fmt formatting library"
)

if(NOT FMT_LIBRARY)
    message(FATAL_ERROR "fmt library not found. Please install libfmt-dev")
endif()
# Link fmt to SvfCore
target_link_libraries(LLVMVectorize PRIVATE ${FMT_LIBRARY})
set_source_files_properties(SLPVectorizer.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")