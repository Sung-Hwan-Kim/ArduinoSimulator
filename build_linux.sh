#!/bin/bash
# ============================================================
#  아두이노 시뮬레이터 - Linux 빌드 (Ubuntu 22.04+/Debian, Windows 의 WSL 포함)
#
#  필요 패키지 (Ubuntu):
#    sudo apt install build-essential cmake ninja-build \
#         qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins libgl-dev fonts-noto-cjk
#
#  사용법:  ./build_linux.sh            → 빌드
#           ./build_linux.sh package    → 빌드 + tar.gz 배포 파일
# ============================================================
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
# /mnt/c, /mnt/d 같은 Windows 드라이브보다 리눅스 파일시스템이 훨씬 빠르므로 기본 빌드 폴더는 홈 아래
BUILD="${BUILD_DIR:-$HOME/build/ArduinoSimulator}"

GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"

cmake -S "$SRC" -B "$BUILD" $GEN -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" --parallel
echo
echo "빌드 완료: $BUILD/ArduinoSimulator"

if [ "$1" = "package" ]; then
    VER=$(sed -n 's/^project(ArduinoSimulator VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")
    ARCH=$(uname -m)
    NAME="ArduinoSimulator-v${VER}-linux-${ARCH}"
    STAGE="$BUILD/package/$NAME"
    rm -rf "$BUILD/package"
    DESTDIR="$STAGE" cmake --install "$BUILD" >/dev/null
    cp "$SRC/LICENSE" "$SRC/README.md" "$SRC/CHANGELOG.md" "$STAGE/"
    cat > "$STAGE/ArduinoSimulator.sh" <<'EOF'
#!/bin/bash
# 압축을 푼 폴더에서 바로 실행 (시스템 Qt6 사용)
exec "$(dirname "$0")/usr/bin/ArduinoSimulator" "$@"
EOF
    chmod +x "$STAGE/ArduinoSimulator.sh"
    tar -C "$BUILD/package" -czf "$BUILD/package/$NAME.tar.gz" "$NAME"
    mkdir -p "$SRC/build/dist"
    cp "$BUILD/package/$NAME.tar.gz" "$SRC/build/dist/"
    echo "배포 파일: build/dist/$NAME.tar.gz"
fi
