project(
  'limit-order-book',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=debug',
  ],
)

py = find_program('python3')

core_cpp = run_command(
  py, '-c',
  'from pathlib import Path; print("\\n".join(str(p) for p in sorted(Path("Core/Source").rglob("*.cpp"))))',
  check: true,
).stdout().strip().split('\n')

app_cpp = run_command(
  py, '-c',
  'from pathlib import Path; print("\\n".join(str(p) for p in sorted(Path("App/Source").rglob("*.cpp"))))',
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

# Test harness — main here, not under Core/Source (not part of libcore).
executable(
  'core_test',
  'Core/Test/Main.cpp',
  dependencies: core_dep,
  install: false,
)
