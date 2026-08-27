project(
  'gtest',
  'cpp',
  version: '1.15.2',
  meson_version: '>=0.50.0',
  default_options: 'cpp_std=c++14',
)

gtest_inc = include_directories(
  'googletest/include',
  'googletest',
  'googlemock/include',
  'googlemock',
)

gtest_lib = static_library(
  'gtest',
  'googletest/src/gtest-all.cc',
  include_directories: gtest_inc,
  cpp_args: ['-DGTEST_HAS_PTHREAD=0'],
)

gtest_main_lib = static_library(
  'gtest_main',
  'googletest/src/gtest_main.cc',
  include_directories: gtest_inc,
  link_with: gtest_lib,
  cpp_args: ['-DGTEST_HAS_PTHREAD=0'],
)

gtest_dep = declare_dependency(
  include_directories: include_directories('googletest/include'),
  link_with: gtest_lib,
)

gtest_main_dep = declare_dependency(
  include_directories: include_directories('googletest/include'),
  link_with: [gtest_lib, gtest_main_lib],
)

gmock_lib = static_library(
  'gmock',
  'googlemock/src/gmock-all.cc',
  include_directories: gtest_inc,
  link_with: gtest_lib,
  cpp_args: ['-DGTEST_HAS_PTHREAD=0'],
)

gmock_dep = declare_dependency(
  include_directories: include_directories('googlemock/include', 'googletest/include'),
  link_with: [gmock_lib, gtest_lib],
)

gmock_main_lib = static_library(
  'gmock_main',
  'googlemock/src/gmock_main.cc',
  include_directories: gtest_inc,
  link_with: [gmock_lib, gtest_lib],
  cpp_args: ['-DGTEST_HAS_PTHREAD=0'],
)

gmock_main_dep = declare_dependency(
  include_directories: include_directories('googlemock/include', 'googletest/include'),
  link_with: [gmock_main_lib, gmock_lib, gtest_lib],
)
