# Makefile for Vulkan Project with CMake Presets
# Supports Linux, macOS, and Windows

# Detect operating system
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        DETECTED_OS := Linux
    endif
    ifeq ($(UNAME_S),Darwin)
        DETECTED_OS := macOS
    endif
endif

# Set platform-specific variables
ifeq ($(DETECTED_OS),Linux)
    CMAKE_PRESET := linux-default
    VULKAN_SDK := $(HOME)/1.3.296.0/x86_64
    EXPORT_LIB_PATH := export LD_LIBRARY_PATH=$(VULKAN_SDK)/lib:$$LD_LIBRARY_PATH
    VULKAN_LAYER_PATH := $(VULKAN_SDK)/share/vulkan/explicit_layer.d
else ifeq ($(DETECTED_OS),macOS)
    CMAKE_PRESET := mac-default
    HOMEBREW_PREFIX := $(shell brew --prefix)
    VULKAN_SDK := $(HOMEBREW_PREFIX)/opt/vulkan-loader
    EXPORT_LIB_PATH := export DYLD_LIBRARY_PATH=$(VULKAN_SDK)/lib:$$DYLD_LIBRARY_PATH
    VULKAN_LAYER_PATH := $(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d
else ifeq ($(DETECTED_OS),Windows)
    CMAKE_PRESET := windows-default
    VULKAN_SDK := C:/VulkanSDK/1.3.296.0
    EXPORT_LIB_PATH :=
    VULKAN_LAYER_PATH := $(VULKAN_SDK)/Bin
else
    $(error Unsupported operating system: $(DETECTED_OS))
endif

# Common environment setup
EXPORT_VULKAN_SDK := export VULKAN_SDK="$(VULKAN_SDK)"
EXPORT_PATH := export PATH="$(VULKAN_SDK)/bin:$$PATH"
EXPORT_VK_LAYER := export VK_LAYER_PATH="$(VULKAN_LAYER_PATH)"

# Combine all environment exports
ifeq ($(DETECTED_OS),Linux)
    ENV_SETUP := $(EXPORT_VULKAN_SDK) && $(EXPORT_PATH) && export LD_LIBRARY_PATH="$(VULKAN_SDK)/lib:$$LD_LIBRARY_PATH" && $(EXPORT_VK_LAYER)
else ifeq ($(DETECTED_OS),macOS)
    ENV_SETUP := export VK_LAYER_PATH="$(VULKAN_LAYER_PATH)" && export DYLD_FALLBACK_LIBRARY_PATH="$(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/lib:$(HOMEBREW_PREFIX)/lib:/usr/local/lib:/usr/lib"
else
    ENV_SETUP := $(EXPORT_VULKAN_SDK) && $(EXPORT_PATH) && $(EXPORT_VK_LAYER)
endif

# Build directory
BUILD_DIR := build
EXECUTABLE := $(BUILD_DIR)/vulkanGLFW

# Colors for output
COLOR_GREEN := \033[0;32m
COLOR_BLUE := \033[0;34m
COLOR_YELLOW := \033[0;33m
COLOR_RESET := \033[0m

.PHONY: all configure build run clean re help info

# Default target
all: build

# Display build information
info:
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)  Vulkan Project Build System$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "Operating System: $(COLOR_GREEN)$(DETECTED_OS)$(COLOR_RESET)"
	@echo "CMake Preset:     $(COLOR_GREEN)$(CMAKE_PRESET)$(COLOR_RESET)"
	@echo "Vulkan SDK:       $(COLOR_GREEN)$(VULKAN_SDK)$(COLOR_RESET)"
	@echo "Build Directory:  $(COLOR_GREEN)$(BUILD_DIR)$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"

# Configure the project with CMake presets
configure: info
	@echo "$(COLOR_YELLOW)Configuring project...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --preset $(CMAKE_PRESET)
	@echo "$(COLOR_GREEN)Configuration complete!$(COLOR_RESET)"

# Build the project
build: configure
	@echo "$(COLOR_YELLOW)Building project...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --build $(BUILD_DIR)
	@echo "$(COLOR_GREEN)Build complete!$(COLOR_RESET)"

# Build without reconfiguring
build-only:
	@echo "$(COLOR_YELLOW)Building project (no configure)...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --build $(BUILD_DIR)
	@echo "$(COLOR_GREEN)Build complete!$(COLOR_RESET)"

# Run the executable
run: build
	@echo "$(COLOR_YELLOW)Running application...$(COLOR_RESET)"
	@$(ENV_SETUP) && ./$(EXECUTABLE)

# Run without building
run-only:
	@echo "$(COLOR_YELLOW)Running application...$(COLOR_RESET)"
	@$(ENV_SETUP) && ./$(EXECUTABLE)

# Clean build artifacts
clean:
	@echo "$(COLOR_YELLOW)Cleaning build directory...$(COLOR_RESET)"
	@rm -rf $(BUILD_DIR)
	@rm -rf vcpkg_installed
	@echo "$(COLOR_GREEN)Clean complete!$(COLOR_RESET)"

# Rebuild from scratch
re: clean build

# Clean only CMake cache (keep compiled objects)
clean-cmake:
	@echo "$(COLOR_YELLOW)Cleaning CMake cache...$(COLOR_RESET)"
	@rm -rf $(BUILD_DIR)/CMakeCache.txt $(BUILD_DIR)/CMakeFiles
	@echo "$(COLOR_GREEN)CMake cache cleaned!$(COLOR_RESET)"

# Reconfigure without full clean
reconfigure: clean-cmake configure

# Install dependencies via vcpkg (if needed)
install-deps:
	@echo "$(COLOR_YELLOW)Installing dependencies via vcpkg...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --preset $(CMAKE_PRESET)
	@echo "$(COLOR_GREEN)Dependencies installed!$(COLOR_RESET)"

# Display help
help:
	@echo "$(COLOR_BLUE)Available targets:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make$(COLOR_RESET) or $(COLOR_GREEN)make all$(COLOR_RESET)      - Configure and build the project"
	@echo "  $(COLOR_GREEN)make info$(COLOR_RESET)              - Display build configuration"
	@echo "  $(COLOR_GREEN)make configure$(COLOR_RESET)         - Configure the project with CMake"
	@echo "  $(COLOR_GREEN)make build$(COLOR_RESET)             - Configure and build the project"
	@echo "  $(COLOR_GREEN)make build-only$(COLOR_RESET)        - Build without reconfiguring"
	@echo "  $(COLOR_GREEN)make run$(COLOR_RESET)               - Build and run the application"
	@echo "  $(COLOR_GREEN)make run-only$(COLOR_RESET)          - Run without building"
	@echo "  $(COLOR_GREEN)make clean$(COLOR_RESET)             - Remove all build artifacts"
	@echo "  $(COLOR_GREEN)make re$(COLOR_RESET)           - Clean and rebuild from scratch"
	@echo "  $(COLOR_GREEN)make clean-cmake$(COLOR_RESET)       - Clean only CMake cache"
	@echo "  $(COLOR_GREEN)make reconfigure$(COLOR_RESET)       - Reconfigure without full clean"
	@echo "  $(COLOR_GREEN)make install-deps$(COLOR_RESET)      - Install dependencies via vcpkg"
	@echo "  $(COLOR_GREEN)make help$(COLOR_RESET)              - Display this help message"
	@echo ""
	@echo "$(COLOR_BLUE)Environment:$(COLOR_RESET)"
	@echo "  VULKAN_SDK=$(VULKAN_SDK)"
	@echo "  VK_LAYER_PATH=$(VULKAN_LAYER_PATH)"
