CXX := clang++
comma := ,

ifdef SANITIZE
CXXFLAGS := -std=c++23 -Wall -Wextra -O1 -g -fno-omit-frame-pointer -fsanitize=$(SANITIZE) -ICore/Source
BUILD := build-make-$(subst $(comma),-,$(SANITIZE))
else ifdef RELEASE
CXXFLAGS := -std=c++23 -Wall -Wextra -O2 -g -DNDEBUG -ICore/Source
BUILD := build-make-release
else
CXXFLAGS := -std=c++23 -Wall -Wextra -O0 -g -ICore/Source
BUILD := build-make
endif

BIN := $(BUILD)/bin
VENV := .venv

CORE_SRCS := $(shell find Core/Source -name '*.cpp')
CORE_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRCS))

DATABENTO_ROOT := vendor/install
DATABENTO_OPENSSL := $(shell brew --prefix openssl@3)
DATABENTO_ZSTD := $(shell brew --prefix zstd)
DATABENTO_CXXFLAGS := $(CXXFLAGS) -mmacosx-version-min=14.6 \
	-I$(DATABENTO_ROOT)/include -I$(DATABENTO_OPENSSL)/include -I$(DATABENTO_ZSTD)/include
DATABENTO_LDFLAGS := -L$(DATABENTO_ROOT)/lib -L$(DATABENTO_ROOT)/lib/databento \
	-L$(DATABENTO_OPENSSL)/lib -L$(DATABENTO_ZSTD)/lib \
	-ldatabento -ldbn_c -lssl -lcrypto -lzstd -lpthread
DATABENTO_BROTLI := $(shell brew --prefix brotli)
DATABENTO_HTTP_LDFLAGS := -L$(DATABENTO_BROTLI)/lib -lbrotlidec -lbrotlienc -lbrotlicommon -lz \
	-framework CoreFoundation -framework CFNetwork -framework Security

.PHONY: all clean core_test itch_dump bench compile_commands databento_smoke debug asan tsan perf profile queue_bench queue_report live databento_replay databento_download mbo_cost mbo_download replay book_plots

all: core_test itch_dump bench

core_test: $(BIN)/core_test
itch_dump: $(BIN)/itch_dump
bench: $(BIN)/bench
queue_bench: $(BIN)/queue_bench

$(BIN)/core_test: $(CORE_OBJS) $(BUILD)/Core/Test/Main.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN)/itch_dump: $(CORE_OBJS) $(BUILD)/App/Source/ItchDump.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN)/bench: $(CORE_OBJS) $(BUILD)/Core/Test/BenchMain.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN)/queue_bench: $(CORE_OBJS) $(BUILD)/Core/Test/QueueBench.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

databento_smoke: $(BIN)/databento_smoke
databento_replay: $(BIN)/databento_replay
databento_download: $(BIN)/databento_download

$(BIN)/databento_smoke: App/Source/DatabentoSmokeTest.cpp App/Source/MboSession.h $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(DATABENTO_CXXFLAGS) $(filter-out %.h,$^) $(DATABENTO_LDFLAGS) -o $@

$(BIN)/databento_replay: App/Source/DatabentoReplay.cpp App/Source/MboSession.h $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(DATABENTO_CXXFLAGS) $(filter-out %.h,$^) $(DATABENTO_LDFLAGS) -o $@

$(BIN)/databento_download: App/Source/DatabentoDownload.cpp
	@mkdir -p $(BIN)
	$(CXX) $(DATABENTO_CXXFLAGS) $^ $(DATABENTO_LDFLAGS) $(DATABENTO_HTTP_LDFLAGS) -o $@

MBO_SYMBOL ?= ESZ6
MBO_START ?= 2026-10-08T00:00
MBO_END ?= 2026-10-08T01:00
MBO_FILE ?= testdata/$(MBO_SYMBOL).mbo.dbn.zst
MBO_PRINT_EVERY ?= 60

mbo_cost: $(BIN)/databento_download
	$(BIN)/databento_download $(MBO_SYMBOL) $(MBO_START) $(MBO_END) $(MBO_FILE)

mbo_download: $(BIN)/databento_download
	$(BIN)/databento_download $(MBO_SYMBOL) $(MBO_START) $(MBO_END) $(MBO_FILE) --confirm

replay:
	$(MAKE) RELEASE=1 databento_replay
	build-make-release/bin/databento_replay $(MBO_FILE) $(MBO_SYMBOL) $(MBO_PRINT_EVERY)

DEPTH_CSV := $(MBO_FILE:.mbo.dbn.zst=.depth.csv)
DEPTH_EVERY ?= 1
DEPTH_LEVELS ?= 40

book_plots: $(VENV)/bin/python
	$(MAKE) RELEASE=1 databento_replay
	build-make-release/bin/databento_replay $(MBO_FILE) $(MBO_SYMBOL) $(DEPTH_EVERY) $(DEPTH_CSV) $(DEPTH_LEVELS)
	$(VENV)/bin/python tools/plot_book.py $(DEPTH_CSV) $(MBO_SYMBOL) plots

LIVE_SYMBOL ?= ESZ6
LIVE_SECONDS ?= 15

live: $(BIN)/databento_smoke
	$(BIN)/databento_smoke $(LIVE_SYMBOL) $(LIVE_SECONDS)

DEBUG_TARGET ?= core_test
DEBUG_ARGS ?= testdata/session_mix.bin

debug: $(BIN)/$(DEBUG_TARGET)
	lldb $< -- $(DEBUG_ARGS)

SAN_TARGET ?= core_test
SAN_ARGS ?= testdata/session_mix.bin

asan:
	$(MAKE) SANITIZE=address,undefined $(SAN_TARGET)
	./build-make-address-undefined/bin/$(SAN_TARGET) $(SAN_ARGS)

tsan:
	$(MAKE) SANITIZE=thread $(SAN_TARGET)
	./build-make-thread/bin/$(SAN_TARGET) $(SAN_ARGS)

PERF_REPEATS ?= 20
PERF_FIXTURE ?= testdata/big_mix.bin
RELEASE_BENCH := build-make-release/bin/bench

$(PERF_FIXTURE):
	python3 testdata/gen_big_mix.py --out $@

perf: $(PERF_FIXTURE)
	$(MAKE) RELEASE=1 bench
	/usr/bin/time -l $(RELEASE_BENCH) $(PERF_REPEATS) $(PERF_FIXTURE)

profile: $(PERF_FIXTURE)
	$(MAKE) RELEASE=1 bench
	rm -rf bench.trace
	xcrun xctrace record --template 'Time Profiler' --output bench.trace --launch -- $(RELEASE_BENCH) $(PERF_REPEATS) $(PERF_FIXTURE)
	open bench.trace

QUEUE_SIZES := 10000 100000 1000000
QUEUE_FIXTURES := $(foreach n,$(QUEUE_SIZES),testdata/queue_$(n).bin)
QUEUE_RUNS ?= 15
QUEUE_ITEMS ?= 5000000
QUEUE_CSV := benchmarks/queue_bench.csv
LATENCY_RATES := 10000 100000 1000000
LATENCY_RUNS ?= 7
LATENCY_CSV := benchmarks/latency.csv
QUEUE_BENCH := build-make-release/bin/queue_bench

testdata/queue_%.bin:
	python3 testdata/gen_big_mix.py --adds $* --locates 8 --mutate-every 4 --out $@

$(VENV)/bin/python:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --quiet matplotlib

queue_report: $(QUEUE_FIXTURES) $(VENV)/bin/python
	$(MAKE) RELEASE=1 queue_bench
	@mkdir -p benchmarks
	echo "bench,impl,size,run,ms" > $(QUEUE_CSV)
	$(QUEUE_BENCH) queue $(QUEUE_ITEMS) $(QUEUE_RUNS) >> $(QUEUE_CSV)
	for f in $(QUEUE_FIXTURES); do $(QUEUE_BENCH) decode $$f $(QUEUE_RUNS) >> $(QUEUE_CSV); done
	echo "bench,impl,rate,run,p50_ns,p99_ns,p999_ns,max_ns" > $(LATENCY_CSV)
	for r in $(LATENCY_RATES); do $(QUEUE_BENCH) latency $$r $(LATENCY_RUNS) >> $(LATENCY_CSV); done
	$(VENV)/bin/python tools/plot_queue_bench.py $(QUEUE_CSV) $(LATENCY_CSV) benchmarks

compile_commands:
	@python3 tools/gen_compile_commands.py

clean:
	rm -rf $(BUILD) build-make-address-undefined build-make-thread
