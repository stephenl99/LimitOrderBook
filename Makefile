ifdef SANITIZE
PRESET := $(if $(findstring thread,$(SANITIZE)),tsan,asan)
else ifdef RELEASE
PRESET := release
else
PRESET := debug
endif

BUILD_ROOT := build-cmake
BIN := $(BUILD_ROOT)/$(PRESET)
DEBUG_BIN := $(BUILD_ROOT)/debug
RELEASE_BIN := $(BUILD_ROOT)/release
VENV := .venv

BINARIES := core_test itch_dump bench queue_bench unit_tests verify_mbo_offsets \
	databento_smoke databento_replay databento_download

define cmake_build
@test -f $(BUILD_ROOT)/$(1)/build.ninja || cmake --preset $(1) > /dev/null
@cmake --build --preset $(1) --target $(2)
endef

.PHONY: all clean test compile_commands $(BINARIES) debug asan tsan perf profile queue_report \
	live mbo_cost mbo_download replay replay_live replay_compare book_plots

all:
	$(call cmake_build,$(PRESET),all)

$(BINARIES):
	$(call cmake_build,$(PRESET),$@)

test:
	$(call cmake_build,debug,all)
	ctest --preset debug

compile_commands:
	cmake --preset debug > /dev/null
	ln -sf $(DEBUG_BIN)/compile_commands.json compile_commands.json

clean:
	rm -rf $(BUILD_ROOT)

DEBUG_TARGET ?= core_test
DEBUG_ARGS ?= testdata/session_mix.bin

debug:
	$(call cmake_build,debug,$(DEBUG_TARGET))
	lldb $(DEBUG_BIN)/$(DEBUG_TARGET) -- $(DEBUG_ARGS)

SAN_TARGET ?= core_test
SAN_ARGS ?= testdata/session_mix.bin

asan:
	$(call cmake_build,asan,$(SAN_TARGET))
	$(BUILD_ROOT)/asan/$(SAN_TARGET) $(SAN_ARGS)

tsan:
	$(call cmake_build,tsan,$(SAN_TARGET))
	$(BUILD_ROOT)/tsan/$(SAN_TARGET) $(SAN_ARGS)

PERF_REPEATS ?= 20
PERF_FIXTURE ?= testdata/big_mix.bin

$(PERF_FIXTURE):
	python3 testdata/gen_big_mix.py --out $@

perf: $(PERF_FIXTURE)
	$(call cmake_build,release,bench)
	/usr/bin/time -l $(RELEASE_BIN)/bench $(PERF_REPEATS) $(PERF_FIXTURE)

profile: $(PERF_FIXTURE)
	$(call cmake_build,release,bench)
	rm -rf bench.trace
	xcrun xctrace record --template 'Time Profiler' --output bench.trace --launch -- $(RELEASE_BIN)/bench $(PERF_REPEATS) $(PERF_FIXTURE)
	open bench.trace

QUEUE_SIZES := 10000 100000 1000000
QUEUE_FIXTURES := $(foreach n,$(QUEUE_SIZES),testdata/queue_$(n).bin)
QUEUE_RUNS ?= 15
QUEUE_ITEMS ?= 5000000
QUEUE_CSV := benchmarks/queue_bench.csv
LATENCY_RATES := 10000 100000 1000000
LATENCY_RUNS ?= 7
LATENCY_CSV := benchmarks/latency.csv
QUEUE_BENCH := $(RELEASE_BIN)/queue_bench

testdata/queue_%.bin:
	python3 testdata/gen_big_mix.py --adds $* --locates 8 --mutate-every 4 --out $@

$(VENV)/bin/python:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --quiet matplotlib

queue_report: $(QUEUE_FIXTURES) $(VENV)/bin/python
	$(call cmake_build,release,queue_bench)
	@mkdir -p benchmarks
	echo "bench,impl,size,run,ms" > $(QUEUE_CSV)
	$(QUEUE_BENCH) queue $(QUEUE_ITEMS) $(QUEUE_RUNS) >> $(QUEUE_CSV)
	for f in $(QUEUE_FIXTURES); do $(QUEUE_BENCH) decode $$f $(QUEUE_RUNS) >> $(QUEUE_CSV); done
	echo "bench,impl,rate,run,p50_ns,p99_ns,p999_ns,max_ns" > $(LATENCY_CSV)
	for r in $(LATENCY_RATES); do $(QUEUE_BENCH) latency $$r $(LATENCY_RUNS) >> $(LATENCY_CSV); done
	$(VENV)/bin/python tools/plot_queue_bench.py $(QUEUE_CSV) $(LATENCY_CSV) benchmarks

LIVE_SYMBOL ?= ESZ6
LIVE_SECONDS ?= 15
LIVE_DATASET ?= GLBX.MDP3

live:
	$(call cmake_build,debug,databento_smoke)
	$(DEBUG_BIN)/databento_smoke $(LIVE_SYMBOL) $(LIVE_SECONDS) $(LIVE_DATASET)

MBO_SYMBOL ?= ESZ6
MBO_START ?= 2026-10-08T00:00
MBO_END ?= 2026-10-08T01:00
MBO_FILE ?= testdata/$(MBO_SYMBOL).mbo.dbn.zst
MBO_PRINT_EVERY ?= 60
MBO_SPEED ?= 0
MBO_THREADING ?= single

mbo_cost:
	$(call cmake_build,debug,databento_download)
	$(DEBUG_BIN)/databento_download $(MBO_SYMBOL) $(MBO_START) $(MBO_END) $(MBO_FILE)

mbo_download:
	$(call cmake_build,debug,databento_download)
	$(DEBUG_BIN)/databento_download $(MBO_SYMBOL) $(MBO_START) $(MBO_END) $(MBO_FILE) --confirm

replay:
	$(call cmake_build,release,databento_replay)
	$(RELEASE_BIN)/databento_replay $(MBO_FILE) $(MBO_SYMBOL) $(MBO_PRINT_EVERY) "" 40 $(MBO_SPEED) $(MBO_THREADING)

replay_live:
	$(MAKE) replay MBO_SPEED=1 MBO_PRINT_EVERY=1 MBO_THREADING=ring

COMPARE_RUNS ?= 25

replay_compare:
	$(call cmake_build,release,databento_replay)
	python3 tools/compare_replay.py $(RELEASE_BIN)/databento_replay $(MBO_FILE) $(MBO_SYMBOL) $(COMPARE_RUNS) benchmarks/replay_threading.csv

DEPTH_CSV := $(MBO_FILE:.mbo.dbn.zst=.depth.csv)
DEPTH_EVERY ?= 1
DEPTH_LEVELS ?= 40

book_plots: $(VENV)/bin/python
	$(call cmake_build,release,databento_replay)
	$(RELEASE_BIN)/databento_replay $(MBO_FILE) $(MBO_SYMBOL) $(DEPTH_EVERY) $(DEPTH_CSV) $(DEPTH_LEVELS)
	$(VENV)/bin/python tools/plot_book.py $(DEPTH_CSV) $(MBO_SYMBOL) plots
