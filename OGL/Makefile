# Compiler and Flags
CXX = g++
CC = gcc
CXXFLAGS = -O3 -Wall -std=c++14
CFLAGS = -O3 -Wall
LDFLAGS = -lwayland-client -lwayland-egl -lEGL -lGL

# Target executable name
TARGET = waygl_desktop

# Object files
OBJS = waygl_desktop.o xdg-shell-protocol.o

# Wayland protocol file locations
WAYLAND_PROTOCOLS_DIR ?= /usr/share/wayland-protocols
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

# Default target
all: $(TARGET)

# Rule to link the final executable
$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Rule to compile the C++ source file
# Note: waygl_desktop.o depends on the generated header to ensure it's created first
waygl_desktop.o: waygl_desktop.cpp xdg-shell-client-protocol.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to compile the generated C protocol file
xdg-shell-protocol.o: xdg-shell-protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to generate the xdg-shell protocol C code using wayland-scanner
xdg-shell-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code $< $@

# Rule to generate the xdg-shell protocol header using wayland-scanner
xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header $< $@

# Clean target to remove compiled binaries and generated code
clean:
	rm -f $(OBJS) $(TARGET) xdg-shell-protocol.c xdg-shell-client-protocol.h

.PHONY: all clean
