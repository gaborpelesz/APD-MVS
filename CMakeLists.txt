cmake_minimum_required (VERSION 3.22)
project (APD)

set(CMAKE_CXX_STANDARD 20)

include(CheckLanguage)
check_language(CUDA)
if (CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    message(" -- CUDA FOUND")
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -O3 --use_fast_math --maxrregcount=128 --ptxas-options=-v --compiler-options -Wall")
    # set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -lineinfo") # for CUDA profiling!
else()
    message(FATAL_ERROR "-- CUDA NOT FOUND: Fatal error")
endif()

find_package(OpenCV REQUIRED)
find_package(Eigen3 REQUIRED)

if(CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-pthread)
    add_definitions(-Wall)
    add_definitions(-Wextra)
    add_definitions(-pedantic)
    add_definitions(-Wno-unused-function)
    add_definitions(-Wno-switch)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math -march=native")
endif()

add_executable(
    APD
    APD.h
    APD.cpp
    APD.cu
    main.cpp
)

target_link_libraries(APD
    PUBLIC
    ${OpenCV_LIBS}
)

target_include_directories(APD
    PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)

set_target_properties(APD
    PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
target_include_directories(
    APD PUBLIC
    ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
)
