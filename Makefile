CC = clang
CFLAGS = -Wall -Wextra -O2 -g -arch arm64 -Iinclude
LDFLAGS = -arch arm64

# Directory layout
SRCDIR = src
INCDIR = include
TESTDIR = tests
TOOLS_DIR = tools
BUILDDIR = build

# Source files
# (We will add more files here as we create them)
JIT_SRCS = $(wildcard $(SRCDIR)/jit/*.c)
RUNTIME_SRCS = $(wildcard $(SRCDIR)/runtime/*.c) \
               $(wildcard $(SRCDIR)/common/*.c) \
               $(wildcard $(SRCDIR)/nvme/*.c)

# Object files
JIT_OBJS = $(JIT_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
RUNTIME_OBJS = $(RUNTIME_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Main library
TARGET_LIB = libjit.a

# Generic compilation rule
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_LIB): $(JIT_OBJS) $(RUNTIME_OBJS)
	ar rcs $@ $^

# Tests
TEST_UNIT_SRCS = $(wildcard $(TESTDIR)/unit/*.c)
TEST_SECURITY_SRCS = $(wildcard $(TESTDIR)/security/*.c)
TEST_INTEGRATION_SRCS = $(wildcard $(TESTDIR)/integration/*.c)
TEST_E2E_SRCS = $(wildcard $(TESTDIR)/e2e/*.c)

TEST_UNIT_BINS = $(TEST_UNIT_SRCS:$(TESTDIR)/unit/%.c=$(BUILDDIR)/tests/unit/%)
TEST_SECURITY_BINS = $(TEST_SECURITY_SRCS:$(TESTDIR)/security/%.c=$(BUILDDIR)/tests/security/%)
TEST_INTEGRATION_BINS = $(TEST_INTEGRATION_SRCS:$(TESTDIR)/integration/%.c=$(BUILDDIR)/tests/integration/%)
TEST_E2E_BINS = $(TEST_E2E_SRCS:$(TESTDIR)/e2e/%.c=$(BUILDDIR)/tests/e2e/%)

TEST_BINS = $(TEST_UNIT_BINS) $(TEST_SECURITY_BINS) $(TEST_INTEGRATION_BINS) $(TEST_E2E_BINS)

# ...

# Ensure test directories exist
directories:
	@mkdir -p $(BUILDDIR)/jit
	@mkdir -p $(BUILDDIR)/runtime
	@mkdir -p $(BUILDDIR)/common
	@mkdir -p $(BUILDDIR)/nvme
	@mkdir -p $(BUILDDIR)/tests/unit
	@mkdir -p $(BUILDDIR)/tests/security
	@mkdir -p $(BUILDDIR)/tests/benchmark
	@mkdir -p $(BUILDDIR)/tests/integration
	@mkdir -p $(BUILDDIR)/tests/e2e
	@mkdir -p $(BUILDDIR)/tools

# ...

# Compile test objects
$(BUILDDIR)/tests/unit/%.o: $(TESTDIR)/unit/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/security/%.o: $(TESTDIR)/security/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/integration/%.o: $(TESTDIR)/integration/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/e2e/%.o: $(TESTDIR)/e2e/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

# Link test binaries
$(BUILDDIR)/tests/unit/%: $(BUILDDIR)/tests/unit/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

$(BUILDDIR)/tests/security/%: $(BUILDDIR)/tests/security/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

$(BUILDDIR)/tests/integration/%: $(BUILDDIR)/tests/integration/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

$(BUILDDIR)/tests/e2e/%: $(BUILDDIR)/tests/e2e/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

# Benchmarks
TEST_BENCH_SRCS = $(wildcard $(TESTDIR)/benchmark/*.c)
TEST_BENCH_BINS = $(TEST_BENCH_SRCS:$(TESTDIR)/benchmark/%.c=$(BUILDDIR)/tests/benchmark/%)

$(BUILDDIR)/tests/benchmark/%.o: $(TESTDIR)/benchmark/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/benchmark/%: $(BUILDDIR)/tests/benchmark/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

# Benchmark targets (specific example, generic rule above)
build/tests/benchmark/test_jit_bench: build/tests/benchmark/test_jit_bench.o libjit.a
	$(CC) $(CFLAGS) $< -L. -ljit -o $@

build/tests/maps/test_maps: build/tests/maps/test_maps.o libjit.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -L. -ljit -o $@

# ...

# Test targets
test: directories $(TEST_BINS)
	@for test in $(TEST_BINS); do echo "Running $$test..."; $$test; done

test-unit: directories $(TEST_UNIT_BINS)
	@echo "=== Running Unit Tests ==="
	@for test in $(TEST_UNIT_BINS); do echo "Running $$test..."; $$test || exit 1; done

test-security: directories $(TEST_SECURITY_BINS)
	@echo "=== Running Security Tests ==="
	@for test in $(TEST_SECURITY_BINS); do echo "Running $$test..."; $$test || exit 1; done

test-integration: directories $(TEST_INTEGRATION_BINS)
	@echo "=== Running Integration Tests ==="
	@for test in $(TEST_INTEGRATION_BINS); do echo "Running $$test..."; $$test || exit 1; done

test-e2e: directories $(TEST_E2E_BINS)
	@echo "=== Running End-to-End Tests ==="
	@for test in $(TEST_E2E_BINS); do echo "Running $$test..."; $$test || exit 1; done

test-benchmark: directories $(TEST_BENCH_BINS)
	@echo "=== Running Performance Benchmarks ==="
	@for test in $(TEST_BENCH_BINS); do echo "Running $$test..."; $$test || exit 1; done

test-all: test-unit test-integration test-e2e test-security test-benchmark
	@echo "=== All Tests Complete ==="

clean:
	rm -rf $(BUILDDIR) $(TARGET_LIB) $(INSPECT_TOOL)

# Fallback for empty wildcard matches
$(JIT_OBJS):
$(RUNTIME_OBJS):
$(TEST_OBJS):
