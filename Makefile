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

# Tests
TEST_SRCS = $(wildcard $(TESTDIR)/unit/*.c)
TEST_OBJS = $(TEST_SRCS:$(TESTDIR)/unit/%.c=$(BUILDDIR)/tests/unit/%.o)
TEST_BINS = $(TEST_SRCS:$(TESTDIR)/unit/%.c=$(BUILDDIR)/tests/unit/%)

# Tools
INSPECT_TOOL = $(BUILDDIR)/tools/inspect_jit

# Ensure test directories exist
directories:
	@mkdir -p $(BUILDDIR)/jit
	@mkdir -p $(BUILDDIR)/runtime
	@mkdir -p $(BUILDDIR)/common
	@mkdir -p $(BUILDDIR)/nvme
	@mkdir -p $(BUILDDIR)/tests/unit
	@mkdir -p $(BUILDDIR)/tools

$(TARGET_LIB): $(JIT_OBJS) $(RUNTIME_OBJS)
	ar rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

# Compile test objects
$(BUILDDIR)/tests/unit/%.o: $(TESTDIR)/unit/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

# Link test binaries
$(BUILDDIR)/tests/unit/%: $(BUILDDIR)/tests/unit/%.o $(TARGET_LIB)
	$(CC) $(LDFLAGS) $< -L. -ljit -o $@

# Build tools
$(INSPECT_TOOL): $(TOOLS_DIR)/inspect_jit.c $(TARGET_LIB)
	$(CC) $(CFLAGS) $< -L. -ljit -o $@

tools: $(INSPECT_TOOL)

test: directories $(TEST_BINS)
	@for test in $(TEST_BINS); do $$test; done

clean:
	rm -rf $(BUILDDIR) $(TARGET_LIB) $(INSPECT_TOOL)

# Fallback for empty wildcard matches
$(JIT_OBJS):
$(RUNTIME_OBJS):
$(TEST_OBJS):
