ASSETS = blackbody_ramp docs scenes textures README.md

ifeq ($(OS),Windows_NT)
	MKDIR = powershell -Command "if (!(Test-Path '$(BUILDDIR)')) { New-Item -ItemType Directory -Path '$(BUILDDIR)' }"
	RMDIR = powershell -Command "if (Test-Path '$(BUILDDIR)') { Remove-Item -Recurse -Force '$(BUILDDIR)' }"

	MKDESTDIR = powershell -Command "if (!(Test-Path '$(DESTDIR)')) { New-Item -ItemType Directory -Path '$(DESTDIR)' }"
	COPY_ASSETS_CMD = powershell -Command "Copy-Item -Path ('$(ASSETS)'.Split(' ')) -Destination '$(DESTDIR)' -Recurse -Force"
	COPY_BIN_CMD = powershell -Command "Copy-Item -Path '$(TARGET)' -Destination '$(DESTDIR)' -Force"

	EXECUTABLE_EXTENSION=.exe
	LIBS=
else
	MKDIR = mkdir -p $(BUILDDIR)
	RMDIR = rm -rf $(BUILDDIR)

	MKDESTDIR = mkdir -p $(DESTDIR)
	COPY_ASSETS_CMD = cp -r $(ASSETS) $(DESTDIR)/
	COPY_BIN_CMD = cp $(TARGET) $(DESTDIR)/

	EXECUTABLE_EXTENSION=
	LIBS=-lm -lpthread
endif

# Compiler and flags
CC = clang

ifeq ($(RELEASE),1)
    CFLAGS = -Wall -Wextra -pedantic -std=c11 -O3
else
    CFLAGS = -Wall -Wextra -pedantic -std=c11 -O3 -g -march=native
endif

ifeq ($(OS),Windows_NT)
	CFLAGS += -D_CRT_SECURE_NO_WARNINGS
endif

LDFLAGS = $(LIBS)

# Directories
SRCDIR = src
BUILDDIR = build

# Source files
SRCS = $(SRCDIR)/main.c $(SRCDIR)/tracer.c $(SRCDIR)/vector.c $(SRCDIR)/color.c \
       $(SRCDIR)/blackbody.c $(SRCDIR)/bloom.c $(SRCDIR)/image.c $(SRCDIR)/config.c \
       $(SRCDIR)/ini.c

# Object files (in build directory)
OBJS = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Path to the executable
TARGET = $(BUILDDIR)/blackhole_tracer$(EXECUTABLE_EXTENSION)

# Default target
all: $(BUILDDIR) $(TARGET)

# Create build directory
$(BUILDDIR):
	$(MKDIR)

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile source files into object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

install:
ifndef DESTDIR
	$(error DESTDIR is undefined)
endif
	$(MKDESTDIR)
	$(COPY_ASSETS_CMD)
	$(COPY_BIN_CMD)

# Clean up build files
clean:
	$(RMDIR)

.PHONY: all clean install
