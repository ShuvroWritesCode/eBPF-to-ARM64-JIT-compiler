CC = clang
CFLAGS = -Wall -Wextra -O2 -g -arch arm64 -Iinclude
LDFLAGS = -arch arm64

# Directory layout
SRCDIR = src
INCDIR = include
TESTDIR = tests
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
# (We will add test targets later)

.PHONY: all clean test directories

all: directories $(TARGET_LIB)

directories:
	@mkdir -p $(BUILDDIR)/jit
	@mkdir -p $(BUILDDIR)/runtime
	@mkdir -p $(BUILDDIR)/common
	@mkdir -p $(BUILDDIR)/nvme

$(TARGET_LIB): $(JIT_OBJS) $(RUNTIME_OBJS)
	ar rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET_LIB)

# Fallback for empty wildcard matches to avoid ar errors
$(JIT_OBJS):
$(RUNTIME_OBJS):
