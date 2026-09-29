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

CORE_SRCS := $(shell find Core/Source -name '*.cpp')
CORE_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRCS))

DATABENTO_ROOT := vendor/install
DATABENTO_OPENSSL := $(shell brew --prefix openssl@3)
DATABENTO_ZSTD := $(shell brew --prefix zstd)
DATABENTO_CXXFLAGS := -std=c++23 -Wall -Wextra -g -ICore/Source -mmacosx-version-min=14.6 \
	-I$(DATABENTO_ROOT)/include -I$(DATABENTO_OPENSSL)/include -I$(DATABENTO_ZSTD)/include
DATABENTO_LDFLAGS := -L$(DATABENTO_ROOT)/lib -L$(DATABENTO_ROOT)/lib/databento \
	-L$(DATABENTO_OPENSSL)/lib -L$(DATABENTO_ZSTD)/lib \
	-ldatabento -ldbn_c -lssl -lcrypto -lzstd -lpthread

.PHONY: all clean core_test app itch_dump bench compile_commands databento_smoke debug asan tsan perf profile

all: core_test app itch_dump bench

core_test: $(BIN)/core_test
app: $(BIN)/app
itch_dump: $(BIN)/itch_dump
bench: $(BIN)/bench

$(BIN)/core_test: $(CORE_OBJS) $(BUILD)/Core/Test/Main.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN)/app: $(CORE_OBJS) $(BUILD)/App/Source/App.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN)/itch_dump: $(CORE_OBJS) $(BUILD)/App/Source/ItchDump.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN)/bench: $(CORE_OBJS) $(BUILD)/Core/Test/BenchMain.o
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

databento_smoke: $(BIN)/databento_smoke

$(BIN)/databento_smoke: App/Source/DatabentoSmokeTest.cpp $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(DATABENTO_CXXFLAGS) $^ $(DATABENTO_LDFLAGS) -o $@

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

compile_commands:
	@python3 tools/gen_compile_commands.py

clean:
	rm -rf $(BUILD) build-make-address-undefined build-make-thread
