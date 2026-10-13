cmake_minimum_required(VERSION 3.24)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 14.6 CACHE STRING "")
endif()

project(LimitOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(LOB_BUILD_TESTS "Build the GoogleTest unit tests" ON)
option(LOB_BUILD_DATABENTO "Build the Databento tools when vendor/install is present" ON)
set(LOB_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined or thread")

file(GLOB_RECURSE LOB_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Core/Source/*.cpp)

find_package(Threads REQUIRED)

add_library(lob_core STATIC ${LOB_CORE_SOURCES})
target_include_directories(lob_core PUBLIC ${CMAKE_SOURCE_DIR}/Core/Source)
target_compile_options(lob_core PUBLIC -Wall -Wextra)
target_link_libraries(lob_core PUBLIC Threads::Threads)

if(LOB_SANITIZE)
  target_compile_options(lob_core PUBLIC -fsanitize=${LOB_SANITIZE} -fno-omit-frame-pointer)
  target_link_options(lob_core PUBLIC -fsanitize=${LOB_SANITIZE})
endif()

function(lob_executable name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE lob_core)
endfunction()

lob_executable(core_test Core/Test/Main.cpp)
lob_executable(bench Core/Test/BenchMain.cpp)
lob_executable(queue_bench Core/Test/QueueBench.cpp)
lob_executable(itch_dump App/Source/ItchDump.cpp)

if(LOB_BUILD_DATABENTO)
  list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/vendor/install)
  if(APPLE)
    foreach(formula openssl@3 zstd brotli)
      execute_process(COMMAND brew --prefix ${formula}
                      OUTPUT_VARIABLE formula_prefix
                      OUTPUT_STRIP_TRAILING_WHITESPACE
                      ERROR_QUIET)
      if(formula_prefix)
        list(APPEND CMAKE_PREFIX_PATH ${formula_prefix})
      endif()
    endforeach()
  endif()

  find_package(databento CONFIG QUIET)
  if(databento_FOUND)
    foreach(tool databento_smoke:DatabentoSmokeTest databento_replay:DatabentoReplay databento_download:DatabentoDownload)
      string(REPLACE ":" ";" parts ${tool})
      list(GET parts 0 tool_name)
      list(GET parts 1 tool_source)
      lob_executable(${tool_name} App/Source/${tool_source}.cpp)
      target_link_libraries(${tool_name} PRIVATE databento::databento)
    endforeach()
  else()
    message(STATUS "databento SDK not found under vendor/install, skipping the Databento tools")
  endif()
endif()

if(LOB_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  include(GoogleTest)

  lob_executable(unit_tests Core/Test/GTestMain.cpp)
  target_link_libraries(unit_tests PRIVATE GTest::gtest_main)
  gtest_discover_tests(unit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  lob_executable(verify_mbo_offsets tools/verify_mbo_offsets.cpp)
  add_test(NAME MboOffsets.RoundTrip COMMAND verify_mbo_offsets)
endif()
