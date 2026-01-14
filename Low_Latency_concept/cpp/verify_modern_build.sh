#!/bin/bash
# Modern C++20 Build Verification Script

set -e  # Exit on error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build_modern_verify"

echo "========================================="
echo "Modern C++20 Build Verification"
echo "========================================="
echo ""

# Clean previous build
echo "[1/4] Cleaning previous build..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Configure
echo "[2/4] Configuring CMake..."
cd "${BUILD_DIR}"
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build modern version
echo "[3/4] Building modern version..."
cmake --build . --target trading_main_modern -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Verify binary
echo "[4/4] Verifying binary..."
if [ -f "${BUILD_DIR}/trading_main_modern" ]; then
    echo ""
    echo "SUCCESS! Modern version built successfully"
    echo ""
    echo "Binary location: ${BUILD_DIR}/trading_main_modern"
    echo "Binary size: $(du -h ${BUILD_DIR}/trading_main_modern | cut -f1)"
    echo ""
    echo "To run:"
    echo "  cd ${BUILD_DIR}"
    echo "  ./trading_main_modern 1 MAKER 10 0.25 100 500 -5000.0"
    echo ""
    
    # Show binary info
    echo "Binary info:"
    file "${BUILD_DIR}/trading_main_modern"
    
    exit 0
else
    echo ""
    echo "FAILED: Binary not found"
    exit 1
fi
