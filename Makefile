# Compiler and flags
CC = /usr/bin/gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O3 -g -march=native # -g for debugging, -O3 for release
LDFLAGS = -lm -lpthread

# Source files directory
SRCDIR = src

# Source files
SRCS = $(SRCDIR)/main.c $(SRCDIR)/tracer.c $(SRCDIR)/vector.c $(SRCDIR)/color.c \
       $(SRCDIR)/blackbody.c $(SRCDIR)/bloom.c $(SRCDIR)/image.c $(SRCDIR)/config.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executable name
TARGET = blackhole_tracer

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile source files into object files
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(SRCDIR)/*.o $(TARGET)

# Phony targets
.PHONY: all clean
