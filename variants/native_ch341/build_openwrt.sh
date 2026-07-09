#!/usr/bin/env bash
# Cross-builds the CH341 companion for OpenWrt (musl) using an OpenWrt SDK.
#
# One-time SDK preparation (download the SDK matching your router's release
# and target from https://downloads.openwrt.org/releases/<ver>/targets/...):
#   tar xf openwrt-sdk-*.tar.* && cd openwrt-sdk-*
#   ./scripts/feeds update base packages
#   ./scripts/feeds install libusb-1.0 argp-standalone i2c-tools
#   make defconfig
#   make package/libusb/compile package/argp-standalone/compile \
#        package/i2c-tools/compile -j$(nproc)
#
# Then:  ./build_openwrt.sh /path/to/openwrt-sdk-...
#
# Deploy: scp .pio/build/Native_CH341_companion_wifi_openwrt/program \
#             root@router:/usr/bin/meshcore-companion
#         ssh root@router 'opkg update && opkg install libusb-1.0 libi2c'
set -e

SDK="$1"
PIO_ENV="${2:-Native_CH341_companion_wifi_openwrt}"
if [ ! -d "$SDK/staging_dir" ]; then
  echo "usage: $0 /path/to/openwrt-sdk [pio-env]" >&2
  exit 1
fi

export STAGING_DIR="$SDK/staging_dir"   # required by the OpenWrt toolchain wrapper

TOOLCHAIN=$(ls -d "$STAGING_DIR"/toolchain-* | head -1)
TARGET=$(ls -d "$STAGING_DIR"/target-* | head -1)
GCC=$(ls "$TOOLCHAIN"/bin/*-openwrt-linux-*gcc 2>/dev/null | grep -v '\-gcc-' | head -1)
PREFIX="${GCC%gcc}"
if [ -z "$GCC" ]; then
  echo "error: no cross-gcc found in $TOOLCHAIN/bin" >&2
  exit 1
fi
echo "toolchain: $PREFIX"
echo "sysroot libs/headers: $TARGET/usr"

export TARGET_CC="${PREFIX}gcc"
export TARGET_CXX="${PREFIX}g++"
export TARGET_AR="${PREFIX}ar"
export TARGET_AS="${PREFIX}as"
export TARGET_LD="${PREFIX}g++"
export TARGET_OBJCOPY="${PREFIX}objcopy"
export TARGET_RANLIB="${PREFIX}ranlib"
export TARGET_CFLAGS="-Os"
export TARGET_CXXFLAGS="-Os"
export TARGET_LDFLAGS=""

# staging headers/libs; argp-standalone for musl; static libstdc++/libgcc so
# the router only needs libusb-1.0 + libi2c from opkg
export PLATFORMIO_BUILD_FLAGS="-I$TARGET/usr/include -L$TARGET/usr/lib -largp -latomic -static-libstdc++ -static-libgcc"

cd "$(dirname "$0")/../.."
${PIO:-pio} run -e "$PIO_ENV"

BIN=".pio/build/$PIO_ENV/program"
"${PREFIX}strip" "$BIN" -o "$BIN.stripped" 2>/dev/null || cp "$BIN" "$BIN.stripped"
echo
echo "built: $BIN.stripped ($(du -h "$BIN.stripped" | cut -f1))"
file "$BIN.stripped" 2>/dev/null || true
