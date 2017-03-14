cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 14)
project(Sleuth)

# Boost
find_package(Boost REQUIRED COMPONENTS filesystem system program_options)

# LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs support core irreader analysis)

# Add sources, link
file(GLOB_RECURSE SRC_LIST src/*.cpp src/*.h)

add_executable(${PROJECT_NAME} ${SRC_LIST})
target_include_directories(${PROJECT_NAME} PUBLIC src)
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} ${llvm_libs})
