project(
  'limit-order-book',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=debug',
  ],
)

if get_option('cpp_std') != 'c++23'
  error('This project requires -Dcpp_std=c++23 (got: ' + get_option('cpp_std') + ')')
endif

py = find_program('python3')

core_cpp = run_command(
  py, '-c',
  'from pathlib import Path; print("\\n".join(str(p) for p in sorted(Path("Core/Source").rglob("*.cpp"))))',
  check: true,
).stdout().strip().split('\n')

# App binaries (exclude dump tool — separate target).
app_cpp = run_command(
  py, '-c',
  'from pathlib import Path; print("\\n".join(str(p) for p in sorted(Path("App/Source").rglob("*.cpp")) if p.name != "ItchDump.cpp"))',
  check: true,
).stdout().strip().split('\n')

core_inc = include_directories('Core/Source')

core_lib = static_library(
  'core',
  core_cpp,
  include_directories: core_inc,
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: core_inc,
)

executable(
  'app',
  app_cpp,
  dependencies: core_dep,
  install: false,
)

# Verbose BinaryFILE / ITCH message dump (not GoogleTest).
executable(
  'itch_dump',
  'App/Source/ItchDump.cpp',
  dependencies: core_dep,
  install: false,
)

# Manual smoke harness (still useful for quick book peek).
executable(
  'core_test',
  'Core/Test/Main.cpp',
  dependencies: core_dep,
  install: false,
)

executable(
  'bench',
  'Core/Test/BenchMain.cpp',
  dependencies: core_dep,
  install: false,
)

# GoogleTest
gtest_dep = dependency('gtest', main: true, fallback: ['gtest', 'gtest_main_dep'])

unit_tests = executable(
  'unit_tests',
  'Core/Test/GTestMain.cpp',
  dependencies: [core_dep, gtest_dep],
  install: false,
)

test('unit_tests', unit_tests, workdir: meson.project_source_root())
