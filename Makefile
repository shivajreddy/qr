# Cross-platform Makefile for Windows and macOS
# Detect OS
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(shell uname -s)
endif

# Compiler
CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra

# Directories
BUILD_DIR := build
SRC_DIR := src
INCLUDE_DIR := include

# Source files
SOURCES := $(SRC_DIR)/main.cpp $(SRC_DIR)/api.cpp

# OS-specific settings
# OS-specific settings
ifeq ($(DETECTED_OS),Windows)
    # Windows settings
    TARGET := $(BUILD_DIR)/main.exe
    CURL_DIR := lib/curl
    INCLUDES := -I$(INCLUDE_DIR) -I$(CURL_DIR)/include
    LIBS := -L$(CURL_DIR)/lib -lcurl -lws2_32

    # Windows commands
    MKDIR := @if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
    RM := @if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
    RUN := $(TARGET)
else ifeq ($(DETECTED_OS),Darwin)
    # macOS settings
    TARGET := $(BUILD_DIR)/main
    # Try Homebrew curl first, fallback to system curl
    CURL_PREFIX := $(shell brew --prefix curl 2>/dev/null || echo "/usr")
    INCLUDES := -I$(INCLUDE_DIR) -I$(CURL_PREFIX)/include
    LIBS := -L$(CURL_PREFIX)/lib -lcurl

    # macOS commands
    MKDIR := @mkdir -p $(BUILD_DIR)
    RM := @rm -rf $(BUILD_DIR)
    COPY_DLL := @: # No-op on macOS
    RUN := ./$(TARGET)
else
    # Linux/other Unix settings
    TARGET := $(BUILD_DIR)/main
    INCLUDES := -I$(INCLUDE_DIR)
    LIBS := -lcurl

    # Unix commands
    MKDIR := @mkdir -p $(BUILD_DIR)
    RM := @rm -rf $(BUILD_DIR)
    COPY_DLL := @: # No-op on Linux
    RUN := ./$(TARGET)
endif

# Build target
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(MKDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET) $(LIBS)
ifeq ($(DETECTED_OS),Windows)
	@echo Copying DLL...
	@cmd /c copy /Y lib\curl\bin\libcurl-x64.dll build\libcurl-x64.dll
	@echo DLL copy complete!
endif
	@echo Build complete for $(DETECTED_OS)!

run: $(TARGET)
	$(RUN)

clean:
	$(RM)
	@echo Clean complete!

rebuild: clean all

# Show detected configuration
info:
	@echo Detected OS: $(DETECTED_OS)
	@echo Target: $(TARGET)
	@echo Compiler: $(CXX)
	@echo Flags: $(CXXFLAGS)
	@echo Includes: $(INCLUDES)
	@echo Libraries: $(LIBS)
ifeq ($(DETECTED_OS),Windows)
	@echo DLL: $(CURL_DLL) -^> $(TARGET_DLL)
endif

.PHONY: all run clean rebuild info
