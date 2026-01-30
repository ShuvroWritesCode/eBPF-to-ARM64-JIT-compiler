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

TEST_UNIT_BINS = $(TEST_UNIT_SRCS:$(TESTDIR)/unit/%.c=$(BUILDDIR)/tests/unit/%)
TEST_SECURITY_BINS = $(TEST_SECURITY_SRCS:$(TESTDIR)/security/%.c=$(BUILDDIR)/tests/security/%)

TEST_BINS = $(TEST_UNIT_BINS) $(TEST_SECURITY_BINS)

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
	@mkdir -p $(BUILDDIR)/tools

# ...

# Compile test objects
$(BUILDDIR)/tests/unit/%.o: $(TESTDIR)/unit/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/security/%.o: $(TESTDIR)/security/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

# Link test binaries
$(BUILDDIR)/tests/unit/%: $(BUILDDIR)/tests/unit/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

$(BUILDDIR)/tests/security/%: $(BUILDDIR)/tests/security/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

# Benchmarks
TEST_BENCH_SRCS = $(wildcard $(TESTDIR)/benchmark/*.c)
TEST_BENCH_BINS = $(TEST_BENCH_SRCS:$(TESTDIR)/benchmark/%.c=$(BUILDDIR)/tests/benchmark/%)

$(BUILDDIR)/tests/benchmark/%.o: $(TESTDIR)/benchmark/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/benchmark/%: $(BUILDDIR)/tests/benchmark/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

# ...

test: directories $(TEST_BINS)
	@for test in $(TEST_BINS); do echo "Running $$test..."; $$test; done

clean:
	rm -rf $(BUILDDIR) $(TARGET_LIB) $(INSPECT_TOOL)

# Fallback for empty wildcard matches
$(JIT_OBJS):
$(RUNTIME_OBJS):
$(TEST_OBJS):
