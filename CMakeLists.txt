# CMakeList.txt : CMake project for restorer, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

set(projectname "restorer")
project (${projectname})

set(SOURCES 
  src/main.cpp
  src/restorer.h
  src/restorer.cpp
  src/rtti.h
  src/rtti.cpp
)

# Add source to this project's executable.
add_executable (${projectname} ${SOURCES})

target_compile_features(${projectname} PRIVATE cxx_std_17)

#######################
# LLVM
find_package(LLVM CONFIG REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
add_definitions(${LLVM_DEFINITIONS})

target_include_directories(${projectname} PRIVATE ${LLVM_INCLUDE_DIRS})

# Find the libraries that correspond to the LLVM components that we wish to use
llvm_map_components_to_libnames(
  llvm_libs Support Core object demangle
  DebugInfoCodeView
  DebugInfoMSF
  DebugInfoPDB
)

IF(MSVC)
  SET_TARGET_PROPERTIES(${projectname} PROPERTIES COMPILE_FLAGS "/EHsc")
ENDIF(MSVC)

# Link against LLVM libraries
target_link_libraries(${projectname} PRIVATE ${llvm_libs})
#######################
