# Compiler and flags
CC = clang
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O3 -g -march=native # -g for debugging, -O3 for release
LDFLAGS = -lm -lpthread

# Directories
SRCDIR = src
BUILDDIR = build

# Source files
SRCS = $(SRCDIR)/main.c $(SRCDIR)/tracer.c $(SRCDIR)/vector.c $(SRCDIR)/color.c \
       $(SRCDIR)/blackbody.c $(SRCDIR)/bloom.c $(SRCDIR)/image.c $(SRCDIR)/config.c \
       $(SRCDIR)/ini.c

# Object files (in build directory)
OBJS = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Executable name
TARGET = $(BUILDDIR)/blackhole_tracer

# Default target
all: $(BUILDDIR) $(TARGET)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile source files into object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -rf $(BUILDDIR)

# Phony targets
