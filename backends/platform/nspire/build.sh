#!/bin/bash
# Build script for ScummVM TI-Nspire CX CAS port
#
# Prerequisites:
#   - Ndless SDK installed (nspire-gcc, genzehn, make-prg)
#   - SDL for Nspire (libSDL.a compiled for ARM)
#
# Usage:
#   cd scummvm
#   ./backends/platform/nspire/build.sh

set -e

# Set up Ndless SDK paths
export NDLESS_SDK="${NDLESS_SDK:-/c/cygwin/ndless-sdk}"
export PATH="$NDLESS_SDK/bin:$NDLESS_SDK/toolchain/install/bin:$PATH"

SCUMMVM_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$SCUMMVM_DIR"

echo "=== ScummVM TI-Nspire CX CAS Build ==="
echo "Using Ndless SDK: $NDLESS_SDK"
echo "ScummVM dir: $SCUMMVM_DIR"
echo ""

# Check for nspire-g++
if ! command -v nspire-g++ &> /dev/null; then
    echo "ERROR: nspire-g++ not found in PATH"
    echo "Please install the Ndless SDK and set NDLESS_SDK environment variable"
    exit 1
fi

# Configure if not already done
if [ ! -f config.mk ]; then
    echo "Running configure..."
    ./configure \
        --host=nspire \
        --disable-all-engines \
        --enable-engine=scumm \
        --disable-mt32emu \
        --disable-hq-scalers \
        --disable-translation \
        --disable-eventrecorder \
        --disable-tts
fi

# Build
echo "Building..."
make -j$(nproc 2>/dev/null || echo 4)

echo ""
echo "=== Build complete ==="
echo "Output: scummvm.tns"
