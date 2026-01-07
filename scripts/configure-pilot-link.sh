#!/bin/bash
set -e

SOURCE_DIR="$1"
PATCH_DIR="$2"
BUILD_DIR="$3"
INSTALL_DIR="$4"

echo "Configuring pilot-link from git source..."
echo "Source: $SOURCE_DIR"
echo "Patches: $PATCH_DIR"
echo "Build: $BUILD_DIR"
echo "Install: $INSTALL_DIR"

# Copy updated config files
echo "Copying config files..."
if [ -f "$PATCH_DIR/config.guess" ]; then
    cp "$PATCH_DIR/config.guess" "$SOURCE_DIR/"
fi
if [ -f "$PATCH_DIR/config.sub" ]; then
    cp "$PATCH_DIR/config.sub" "$SOURCE_DIR/"
fi

# Apply patches (check if already applied)
echo "Applying patches..."
cd "$SOURCE_DIR"
for patch in "$PATCH_DIR"/*.patch; do
    if [ -f "$patch" ]; then
        echo "  Applying $(basename $patch)..."
        # Try to apply, but don't fail if already applied
        patch -N -p1 < "$patch" 2>/dev/null || echo "  (already applied or skipped)"
    fi
done

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run autogen.sh (which handles autoreconf + configure)
echo "Running autogen.sh..."
"$SOURCE_DIR/autogen.sh" \
    --prefix="$INSTALL_DIR" \
    --enable-conduits \
    --enable-libusb \
    --with-libiconv \
    --with-libpng \
    --disable-shared \
    --enable-static

echo "Configuration complete!"
