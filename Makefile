CXX := clang++

ifdef RELEASE
CXXFLAGS := -std=c++23 -Wall -Wextra -O2 -DNDEBUG -ICore/Source
BUILD := build-make-release
else
CXXFLAGS := -std=c++23 -Wall -Wextra -O0 -g -ICore/Source
BUILD := build-make
endif

BIN := $(BUILD)/bin

CORE_SRCS := $(shell find Core/Source -name '*.cpp')
CORE_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRCS))

.PHONY: all clean core_test app itch_dump bench compile_commands

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
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Regenerates compile_commands.json for clangd/IDE tooling.
compile_commands:
	@python3 tools/gen_compile_commands.py

clean:
	rm -rf $(BUILD)
