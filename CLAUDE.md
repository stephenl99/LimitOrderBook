# LimitOrderBook

C++23 limit order book. ITCH file replay + a Databento live-feed integration in progress. CMake is the build (`CMakeLists.txt`, presets `debug`, `release`, `asan`, `tsan` in `CMakePresets.json`, output in `build-cmake/<preset>/`). The `Makefile` is a thin wrapper over it plus the benchmark, plot and replay workflows: `make`, `make test`, `make queue_report`, `make replay`. Meson was dropped.

## Code style

No comments in code. Names and structure should carry the meaning. Put anything else — rationale, open questions, caveats — in the PR description or chat, not inline.
