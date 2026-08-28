#!/usr/bin/env sh

set -eu

IMAGE_NAME="${IMAGE_NAME:-mpxcast-dev}"
TARGET="native"
BUILD_DIR=""
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
RUN_AFTER_BUILD="0"
CLEAN_ONLY="0"

usage() {
    cat <<'EOF'
Usage: scripts/build-in-docker.sh [options]

Options:
  --native        Build for the container's native architecture
  --arm32         Cross-compile a 32-bit ARM (armhf) binary
  env CMAKE_BUILD_TYPE=...  Override the CMake build type (default: Release)
  --build-dir DIR Override the build directory
  --clean         Remove the whole build directory and exit
  --run           Run the built executable after a successful build
  -h, --help      Show this help text
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --native)
            TARGET="native"
            ;;
        --arm32)
            TARGET="arm32"
            ;;
        --build-dir)
            shift
            if [ "$#" -eq 0 ]; then
                echo "error: --build-dir requires a value" >&2
                exit 1
            fi
            BUILD_DIR="$1"
            ;;
        --clean)
            CLEAN_ONLY="1"
            ;;
        --run)
            RUN_AFTER_BUILD="1"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

if [ -z "$BUILD_DIR" ]; then
    if [ "$TARGET" = "arm32" ]; then
        BUILD_DIR="build/armv7"
    else
        BUILD_DIR="build/native"
    fi
fi

REPO_ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ "$CLEAN_ONLY" = "1" ]; then
    echo "Removing build directory: $REPO_ROOT/build"
    rm -rf "$REPO_ROOT/build"
    exit 0
fi

BUILD_IMAGE_CMD="docker build -t $IMAGE_NAME -f docker/Dockerfile ."
RUN_PREFIX="docker run --rm"

CONFIGURE_CMD="cmake -S . -B $BUILD_DIR -DCMAKE_BUILD_TYPE=$BUILD_TYPE"

if [ "$TARGET" = "arm32" ] && [ ! -f "$REPO_ROOT/$BUILD_DIR/CMakeCache.txt" ]; then
    CONFIGURE_CMD="$CONFIGURE_CMD -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake"
fi

echo "Building Docker image: $IMAGE_NAME"
(cd "$REPO_ROOT" && sh -c "$BUILD_IMAGE_CMD")

CONTAINER_CMD=$(cat <<EOF
$CONFIGURE_CMD
cmake --build $BUILD_DIR
EOF
)

if [ "$RUN_AFTER_BUILD" = "1" ]; then
    if [ "$TARGET" = "arm32" ]; then
        CONTAINER_CMD="$CONTAINER_CMD
readelf -h $BUILD_DIR/mpxcast"
    else
        CONTAINER_CMD="$CONTAINER_CMD
$BUILD_DIR/mpxcast"
    fi
fi

echo "Building project in container using build directory: $BUILD_DIR"
exec sh -c "
    cd \"$REPO_ROOT\" && \
    $RUN_PREFIX -v \"$REPO_ROOT:/workspace\" -w /workspace $IMAGE_NAME \
    bash -lc '$CONTAINER_CMD'
"
