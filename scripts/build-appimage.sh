#!/bin/bash
#
# build-appimage.sh - Build Wild Palms AppImage
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
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DWILDPALMS_INSTALLED=ON \
    -DKDE_INSTALL_PLUGINDIR=plugins \
    -DBUILD_TESTS=OFF

# Build
log_info "Building Wild Palms..."
make -j$(nproc)

# Install to AppDir
log_info "Installing to AppDir..."
rm -rf "$APPDIR"
make install DESTDIR="$APPDIR"

# Verify desktop file and icon are installed
if [ ! -f "$APPDIR/usr/share/applications/ca.vibekoder.wildpalms.desktop" ]; then
    log_error "Desktop file not found in AppDir!"
    exit 1
fi

if [ ! -f "$APPDIR/usr/share/icons/hicolor/scalable/apps/ca.vibekoder.wildpalms.svg" ]; then
    log_error "Icon not found in AppDir!"
    exit 1
fi

# Bundle Breeze icons — only the icons the app actually uses, at common sizes.
# This avoids shipping the full 112 MB Breeze theme while ensuring the AppImage
# looks correct on non-KDE desktops.
BREEZE_ICONS="/usr/share/icons/breeze"
APPDIR_ICONS="$APPDIR/usr/share/icons/breeze"

if [ -d "$BREEZE_ICONS" ]; then
    log_info "Bundling Breeze icons..."

    # Icons used by Wild Palms (from QIcon::fromTheme calls)
    ICON_NAMES=(
        # Actions
        document-new document-open document-open-folder document-save
        document-import document-open-recent
        edit-delete edit-clear edit-clear-history edit-copy edit-undo
        view-refresh view-calendar view-preview view-task
        view-pim-notes view-pim-contacts
        folder-new folder-open folder-sync folder-documents folder
        go-previous go-next list-add
        network-connect network-disconnect
        configure system-run download
        # Devices
        phone drive-removable-media
        # Status / Dialog
        dialog-ok-apply dialog-warning dialog-question dialog-cancel dialog-messages
        user-identity preferences-plugin checkbox
        # Mimetypes
        text-html internet-web-browser
        application-x-generic
    )

    SIZES=(16 22 24 32 48)

    for size in "${SIZES[@]}"; do
        for icon in "${ICON_NAMES[@]}"; do
            for category in actions devices status mimetypes places apps; do
                src="$BREEZE_ICONS/$category/$size/$icon.svg"
                if [ -f "$src" ]; then
                    dest="$APPDIR_ICONS/$category/$size"
                    mkdir -p "$dest"
                    cp "$src" "$dest/"
                fi
            done
        done
    done

    # Copy the index.theme so Qt recognises this as a valid icon theme
    if [ -f "$BREEZE_ICONS/index.theme" ]; then
        cp "$BREEZE_ICONS/index.theme" "$APPDIR_ICONS/"
    fi

    ICON_COUNT=$(find "$APPDIR_ICONS" -name "*.svg" 2>/dev/null | wc -l)
    log_info "Bundled $ICON_COUNT Breeze icon files"
else
    log_warn "Breeze icons not found at $BREEZE_ICONS — AppImage will rely on system theme"
fi

# Bundle Qt SVG icon engine — linuxdeploy-plugin-qt does NOT include this
# automatically, and without it QIcon::fromTheme() cannot load .svg icons at all.
QMAKE_PROBE=$(which qmake6 2>/dev/null || which qmake 2>/dev/null)
QT_PLUGINS_SYS=$($QMAKE_PROBE -query QT_INSTALL_PLUGINS)
SVG_ENGINE="$QT_PLUGINS_SYS/iconengines/libqsvgicon.so"
if [ -f "$SVG_ENGINE" ]; then
    mkdir -p "$APPDIR/usr/plugins/iconengines"
    cp "$SVG_ENGINE" "$APPDIR/usr/plugins/iconengines/"
    log_info "Bundled SVG icon engine plugin"
else
    log_warn "SVG icon engine not found at $SVG_ENGINE — themed icons will not render"
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
VERSION=$(grep "project(WildPalms VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed 's/.*VERSION \([0-9.]*\).*/\1/')
export VERSION

log_info "Building AppImage for Wild Palms v$VERSION..."

# Run linuxdeploy with Qt plugin
cd "$BUILD_DIR"
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/ca.vibekoder.wildpalms.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/ca.vibekoder.wildpalms.svg"

# Find the generated AppImage
APPIMAGE=$(ls -1 Wild_Palms*.AppImage 2>/dev/null || ls -1 WildPalms*.AppImage 2>/dev/null | head -1)

if [ -n "$APPIMAGE" ]; then
    log_info "AppImage created successfully: $BUILD_DIR/$APPIMAGE"
    log_info ""
    log_info "To run: ./$APPIMAGE"
    log_info "To install system-wide, copy to /usr/local/bin/ or ~/bin/"
else
    log_error "AppImage creation failed!"
    exit 1
fi
