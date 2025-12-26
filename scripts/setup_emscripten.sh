#!/bin/bash
# Emscripten SDK Auto-Setup Script
# This script automatically installs Emscripten SDK for WebAssembly builds

set -e  # Exit on error

# Configuration
EMSDK_DIR="${HOME}/emsdk"
EMSDK_VERSION="3.1.71"
COLOR_GREEN="\033[0;32m"
COLOR_BLUE="\033[0;34m"
COLOR_YELLOW="\033[0;33m"
COLOR_RESET="\033[0m"

echo -e "${COLOR_BLUE}========================================${COLOR_RESET}"
echo -e "${COLOR_BLUE}  Emscripten SDK Setup${COLOR_RESET}"
echo -e "${COLOR_BLUE}========================================${COLOR_RESET}"

# Check if Emscripten is already installed
if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo -e "${COLOR_GREEN}✅ Emscripten already installed at $EMSDK_DIR${COLOR_RESET}"

    # Activate and show version
    source "$EMSDK_DIR/emsdk_env.sh" 2>/dev/null
    echo ""
    echo -e "${COLOR_BLUE}Installed version:${COLOR_RESET}"
    emcc --version | head -n 1

    echo ""
    echo -e "${COLOR_GREEN}To use Emscripten in your current shell:${COLOR_RESET}"
    echo -e "  ${COLOR_YELLOW}source ~/emsdk/emsdk_env.sh${COLOR_RESET}"
    echo ""
    echo -e "${COLOR_GREEN}To build for WebAssembly:${COLOR_RESET}"
    echo -e "  ${COLOR_YELLOW}make wasm${COLOR_RESET}"
    exit 0
fi

# Check for git
if ! command -v git &> /dev/null; then
    echo -e "${COLOR_YELLOW}⚠️  git not found. Please install git first.${COLOR_RESET}"
    exit 1
fi

# Clone Emscripten SDK
echo -e "${COLOR_BLUE}📦 Cloning Emscripten SDK...${COLOR_RESET}"
git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"

# Enter directory
cd "$EMSDK_DIR"

# Install specific version
echo ""
echo -e "${COLOR_BLUE}📥 Installing Emscripten $EMSDK_VERSION...${COLOR_RESET}"
./emsdk install $EMSDK_VERSION

# Activate version
echo ""
echo -e "${COLOR_BLUE}🔧 Activating Emscripten $EMSDK_VERSION...${COLOR_RESET}"
./emsdk activate $EMSDK_VERSION

# Source environment
source "$EMSDK_DIR/emsdk_env.sh"

# Verify installation
echo ""
echo -e "${COLOR_GREEN}========================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}✅ Emscripten installed successfully!${COLOR_RESET}"
echo -e "${COLOR_GREEN}========================================${COLOR_RESET}"
echo ""
emcc --version | head -n 1
echo ""

# Instructions
echo -e "${COLOR_BLUE}Next steps:${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}1. Activate Emscripten in your current shell:${COLOR_RESET}"
echo -e "   ${COLOR_GREEN}source ~/emsdk/emsdk_env.sh${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}2. Build for WebAssembly:${COLOR_RESET}"
echo -e "   ${COLOR_GREEN}make wasm${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}3. Serve and test:${COLOR_RESET}"
echo -e "   ${COLOR_GREEN}make serve-wasm${COLOR_RESET}"
echo -e "   ${COLOR_BLUE}Then open: http://localhost:8000${COLOR_RESET}"
echo ""

# Add to shell profile (optional prompt)
echo -e "${COLOR_YELLOW}Optional: Add Emscripten to your shell profile?${COLOR_RESET}"
echo -e "This will automatically activate Emscripten in new terminals."
read -p "Add to ~/.bashrc? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if ! grep -q "emsdk_env.sh" ~/.bashrc; then
        echo "" >> ~/.bashrc
        echo "# Emscripten SDK" >> ~/.bashrc
        echo "source ~/emsdk/emsdk_env.sh &>/dev/null" >> ~/.bashrc
        echo -e "${COLOR_GREEN}✅ Added to ~/.bashrc${COLOR_RESET}"
    else
        echo -e "${COLOR_BLUE}Already present in ~/.bashrc${COLOR_RESET}"
    fi
fi

echo ""
echo -e "${COLOR_GREEN}Setup complete! 🎉${COLOR_RESET}"
