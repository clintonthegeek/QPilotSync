#!/bin/bash
#
# build-appimage.sh - Build QPilotSync AppImage
#
# This script creates a portable AppImage that can run on most Linux distributions.
# It downloads linuxdeploy tools if needed and bundles all dependencies.
#
# Usage: ./scripts/build-appimage.sh [--clean]
#
# Options:
#   --clean    Remove existing build directory before building
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$PROJECT_DIR/tools"

# Tool URLs (x86_64)
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Parse arguments
CLEAN_BUILD=0
for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN_BUILD=1
            ;;
        --help|-h)
            echo "Usage: $0 [--clean]"
            echo ""
            echo "Options:"
            echo "  --clean    Remove existing build directory before building"
            exit 0
            ;;
    esac
done

# Check dependencies
log_info "Checking build dependencies..."

check_command() {
    if ! command -v "$1" &> /dev/null; then
        log_error "$1 is required but not installed."
        exit 1
    fi
}

check_command cmake
check_command make
check_command wget

# Create tools directory
mkdir -p "$TOOLS_DIR"

# Download linuxdeploy if not present
download_tool() {
    local name="$1"
    local url="$2"
    local path="$TOOLS_DIR/$name"

    if [ ! -f "$path" ]; then
        log_info "Downloading $name..."
        wget -q --show-progress -O "$path" "$url"
        chmod +x "$path"
    else
        log_info "$name already present"
    fi
}

download_tool "linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY_URL"
download_tool "linuxdeploy-plugin-qt-x86_64.AppImage" "$LINUXDEPLOY_QT_URL"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

# Clean build if requested
if [ "$CLEAN_BUILD" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    log_info "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
log_info "Configuring build..."
cmake "$PROJECT_DIR" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

# Build
log_info "Building QPilotSync..."
make -j$(nproc)

# Install to AppDir
log_info "Installing to AppDir..."
rm -rf "$APPDIR"
make install DESTDIR="$APPDIR"

# Verify desktop file and icon are installed
if [ ! -f "$APPDIR/usr/share/applications/org.qpilotsync.QPilotSync.desktop" ]; then
    log_error "Desktop file not found in AppDir!"
    exit 1
fi

if [ ! -f "$APPDIR/usr/share/icons/hicolor/scalable/apps/org.qpilotsync.QPilotSync.svg" ]; then
    log_error "Icon not found in AppDir!"
    exit 1
fi

# Find Qt's qmake
QMAKE_PATH=$(which qmake6 2>/dev/null || which qmake 2>/dev/null)
if [ -z "$QMAKE_PATH" ]; then
    log_error "qmake not found. Please ensure Qt6 development tools are installed."
    exit 1
fi
log_info "Using qmake: $QMAKE_PATH"

# Set up environment for linuxdeploy
export QMAKE="$QMAKE_PATH"

# Get Qt paths from qmake
QT_PLUGIN_PATH=$($QMAKE_PATH -query QT_INSTALL_PLUGINS)
QT_LIB_PATH=$($QMAKE_PATH -query QT_INSTALL_LIBS)

export QT_PLUGIN_PATH
export LD_LIBRARY_PATH="$QT_LIB_PATH:$APPDIR/usr/lib:$LD_LIBRARY_PATH"

# Disable stripping - linuxdeploy's bundled strip doesn't support newer ELF formats
# (.relr.dyn sections used by modern distros like Manjaro)
export NO_STRIP=1

# Add version to output filename
VERSION=$(grep "project(QPilotSync VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')
export VERSION

log_info "Building AppImage for QPilotSync v$VERSION..."

# Run linuxdeploy with Qt plugin
cd "$BUILD_DIR"
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/org.qpilotsync.QPilotSync.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/org.qpilotsync.QPilotSync.svg"

# Find the generated AppImage
APPIMAGE=$(ls -1 QPilotSync*.AppImage 2>/dev/null | head -1)

if [ -n "$APPIMAGE" ]; then
    log_info "AppImage created successfully: $BUILD_DIR/$APPIMAGE"
    log_info ""
    log_info "To run: ./$APPIMAGE"
    log_info "To install system-wide, copy to /usr/local/bin/ or ~/bin/"
else
    log_error "AppImage creation failed!"
    exit 1
fi
