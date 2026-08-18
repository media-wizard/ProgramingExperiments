# Ubuntu 22.04:
#   sudo apt install build-essential pkg-config libglfw3-dev libglew-dev \
#                    libfreetype-dev fonts-dejavu-core

CXX      := g++
PKGS     := glfw3 glew freetype2
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra $(shell pkg-config --cflags $(PKGS))
LDLIBS   := $(shell pkg-config --libs $(PKGS)) -lGL

TARGET := glclock
SRC    := main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
