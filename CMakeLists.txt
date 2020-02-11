cmake_minimum_required(VERSION 3.16)
include(ExternalProject)

project(rsync_backup)

# Settings
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# My library
set(FPR_LIB_DIR "/home/shinjitumala/E_DRIVE/Project/C++/FPR_LIB")
ExternalProject_Add(
    FPR_LIB
    SOURCE_DIR "${FPR_LIB_DIR}"
    BINARY_DIR "${FPR_LIB_DIR}/build"
    STEP_TARGETS build
    EXCLUDE_FROM_ALL TRUE
)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
llvm_map_components_to_libnames(REQ_LLVM_LIBRARIES
    support
)

add_executable(rsync_backup)
add_dependencies(rsync_backup FPR_LIB-build)

add_compile_options(
    -Wall
)

add_definitions(
    ${LLVM_DEFINITIONS}
)

target_link_directories(rsync_backup
    PRIVATE ${FPR_LIB_DIR}/build
    PRIVATE ${LLVM_LINK_DIRS}
)
target_link_libraries(rsync_backup
    FPR
    ${REQ_LLVM_LIBRARIES}
)
target_include_directories(rsync_backup
    PRIVATE include
    PRIVATE ${FPR_LIB_DIR}/include
    PRIVATE ${LLVM_INCLUDE_DIRS}
)

add_subdirectory(src)