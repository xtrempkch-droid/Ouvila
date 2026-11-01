#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <device> <variant>" >&2
    echo "Example: $0 powkiddy-x55 kodi" >&2
}

if [[ $# -ne 2 ]]; then
    usage
    exit 2
fi

DEVICE=$1
VARIANT=$2

if [[ ! $DEVICE =~ ^[a-z0-9]+(-[a-z0-9]+)*$ || ! $VARIANT =~ ^[a-z0-9]+(-[a-z0-9]+)*$ ]]; then
    echo "Device and variant must be lowercase identifiers containing letters, digits, and hyphens." >&2
    exit 2
fi

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$ROOT_DIR"
DEVICE_CONFIG="$ROOT_DIR/devices/$DEVICE/device.conf"
VARIANT_CONFIG="$ROOT_DIR/variants/$VARIANT/variant.conf"

read_config_value() {
    local key=$1
    local file=$2
    local count
    count=$(grep -c "^${key}=" "$file" || true)
    if [[ $count -ne 1 ]]; then
        echo "Expected exactly one '$key' entry in $file." >&2
        return 1
    fi
    sed -n "s/^${key}=//p" "$file"
}

read_optional_config_value() {
    local key=$1
    local file=$2
    local default=$3
    local count
    count=$(grep -c "^${key}=" "$file" || true)
    if [[ $count -eq 0 ]]; then
        printf '%s\n' "$default"
        return 0
    fi
    if [[ $count -ne 1 ]]; then
        echo "Expected at most one '$key' entry in $file." >&2
        return 1
    fi
    sed -n "s/^${key}=//p" "$file"
}

resolve_repo_path() {
    local raw_path=$1
    local label=$2

    if [[ -z $raw_path ]]; then
        echo "Required path for $label is empty." >&2
        return 1
    fi
    if [[ $raw_path == *".."* ]]; then
        echo "Path traversal is not allowed for $label: $raw_path" >&2
        return 1
    fi

    if [[ $raw_path == /* ]]; then
        printf '%s\n' "$raw_path"
    else
        printf '%s\n' "$ROOT_DIR/$raw_path"
    fi
}

if [[ ! -f $DEVICE_CONFIG ]]; then
    echo "Unknown device '$DEVICE' (expected $DEVICE_CONFIG)." >&2
    exit 2
fi
if [[ ! -f $VARIANT_CONFIG ]]; then
    echo "Unknown variant '$VARIANT' (expected $VARIANT_CONFIG)." >&2
    exit 2
fi

CONFIG_DEVICE=$(read_config_value id "$DEVICE_CONFIG")
CONFIG_VARIANT=$(read_config_value id "$VARIANT_CONFIG")
VARIANT_STATUS=$(read_config_value status "$VARIANT_CONFIG")
PACKAGE_MANIFEST=$(read_config_value packages "$VARIANT_CONFIG")
DTB_NAME=$(read_config_value image_dtb "$DEVICE_CONFIG")
DISPLAY_TRANSFORM=$(read_optional_config_value display_transform "$DEVICE_CONFIG" "")
if [[ $CONFIG_DEVICE != "$DEVICE" || $CONFIG_VARIANT != "$VARIANT" ]]; then
    echo "Configuration identifiers must match their directory names." >&2
    exit 2
fi
if [[ $VARIANT_STATUS != available ]]; then
    echo "Variant '$VARIANT' is not buildable (status: $VARIANT_STATUS)." >&2
    exit 2
fi
if [[ -z $DTB_NAME ]]; then
    echo "Device '$DEVICE' has no confirmed image DTB and cannot be packaged." >&2
    exit 2
fi
if [[ ! $DTB_NAME =~ ^[a-zA-Z0-9._-]+\.dtb$ ]]; then
    echo "Invalid image_dtb value in $DEVICE_CONFIG." >&2
    exit 2
fi
if [[ -n $DISPLAY_TRANSFORM && ! $DISPLAY_TRANSFORM =~ ^(auto|normal|rotate-90|rotate-180|rotate-270|flipped|flipped-rotate-90|flipped-rotate-180|flipped-rotate-270)$ ]]; then
    echo "Invalid display_transform value in $DEVICE_CONFIG: $DISPLAY_TRANSFORM" >&2
    exit 2
fi
if [[ ! $PACKAGE_MANIFEST =~ ^[a-zA-Z0-9._/-]+$ || $PACKAGE_MANIFEST == /* || $PACKAGE_MANIFEST == *..* ]]; then
    echo "Invalid packages value in $VARIANT_CONFIG." >&2
    exit 2
fi
if [[ ! -f $ROOT_DIR/variants/$VARIANT/$PACKAGE_MANIFEST ]]; then
    echo "Package manifest not found: $ROOT_DIR/variants/$VARIANT/$PACKAGE_MANIFEST" >&2
    exit 2
fi

UBOOT_DIR=$(resolve_repo_path "${OUVILA_UBOOT_DIR:-uboot}" "OUVILA_UBOOT_DIR") || exit 1
KERNEL_DIR=$(resolve_repo_path "${OUVILA_KERNEL_DIR:-kernel}" "OUVILA_KERNEL_DIR") || exit 1
ROOTFS_DIR=$(resolve_repo_path "${OUVILA_ROOTFS_DIR:-rootfs}" "OUVILA_ROOTFS_DIR") || exit 1
OUTPUT_DIR=$(resolve_repo_path "${OUVILA_OUTPUT_DIR:-output}" "OUVILA_OUTPUT_DIR") || exit 1
IMAGE_NAME=${OUVILA_IMAGE_NAME:-ouvila-${DEVICE}-${VARIANT}.img}
if [[ ! $IMAGE_NAME =~ ^[a-zA-Z0-9._-]+\.img$ ]]; then
    echo "OUVILA_IMAGE_NAME must be a filename ending in .img." >&2
    exit 2
fi

for directory in "$UBOOT_DIR" "$KERNEL_DIR" "$ROOTFS_DIR"; do
    if [[ ! -d $directory ]]; then
        echo "Required build directory not found: $directory" >&2
        exit 1
    fi
done

if [[ -n ${OUVILA_DTB_SOURCE:-} ]]; then
    DTB_SOURCE=$(resolve_repo_path "$OUVILA_DTB_SOURCE" "OUVILA_DTB_SOURCE") || exit 1
else
    DTB_SOURCE="$KERNEL_DIR/arch/arm64/boot/dts/rockchip/$DTB_NAME"
fi
if [[ ! -f $DTB_SOURCE ]]; then
    echo "Required DTB not found: $DTB_SOURCE" >&2
    exit 1
fi

mkdir -p -- "$OUTPUT_DIR"
IMAGE_PATH="$OUTPUT_DIR/$IMAGE_NAME"
if [[ -e $IMAGE_PATH || -e $IMAGE_PATH.gz ]]; then
    echo "Refusing to overwrite existing image: $IMAGE_PATH[.gz]" >&2
    exit 1
fi

"$ROOT_DIR/build/create-image.sh" \
    "$IMAGE_PATH" "$UBOOT_DIR" "$KERNEL_DIR" "$ROOTFS_DIR" \
    "$DEVICE" "$VARIANT" "$DTB_SOURCE" "$DTB_NAME" "$DISPLAY_TRANSFORM"
