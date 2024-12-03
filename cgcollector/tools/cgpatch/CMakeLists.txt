project(CGPatch)

# LLVM
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

option(
  CGPATCH_USE_MPI
  "On or Of"
  ON
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

# Create Libraries
add_library(cgpatch-pass SHARED cgpatch-pass.cpp)
add_library(cgpatch-runtime STATIC cgpatch-runtime.cpp SymbolRetriever/SymbolRetriever.cpp)

target_include_directories(cgpatch-runtime PUBLIC SymbolRetriever)
target_link_libraries(cgpatch-pass ${llvm_libs})

if(CGPATCH_USE_MPI)
  target_compile_definitions(cgpatch-runtime PRIVATE USE_MPI=1)
endif()

# MetaCG
add_metacg(cgpatch-runtime)
add_spdlog_libraries(cgpatch-runtime)
add_json(cgpatch-runtime)

add_metacg(cgpatch-pass)
add_spdlog_libraries(cgpatch-pass)
add_json(cgpatch-pass)
