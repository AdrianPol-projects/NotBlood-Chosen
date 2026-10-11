#!/bin/sh
# Downloads the SDL2 and gl4es sources the Android build needs into third_party/ (the same
# versions as the iPad build) and applies NotBlood's gl4es patches.
set -e
cd "$(dirname "$0")"
SDL_VERSION=2.32.10
GL4ES_COMMIT=ec16bedd8819c475326f4f1a3063772c6d986e06
mkdir -p third_party
if [ ! -f third_party/SDL2/CMakeLists.txt ]; then
    curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz" | tar xz -C third_party
    mv "third_party/SDL2-$SDL_VERSION" third_party/SDL2
fi
if [ ! -f third_party/gl4es/src/gl/gl4es.c ]; then
    curl -fsSL "https://github.com/ptitSeb/gl4es/archive/$GL4ES_COMMIT.tar.gz" | tar xz -C third_party
    mv "third_party/gl4es-$GL4ES_COMMIT" third_party/gl4es
    for p in ../iOS/gl4es/*.patch; do
        patch -d third_party/gl4es -p1 --forward < "$p"
    done
fi
echo "SDL2 and gl4es are in $(pwd)/third_party"
