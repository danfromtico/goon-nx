# Linux (aarch64) build + headless run of goon-nx:
# SDL2 + OpenGL through Mesa llvmpipe, SDL_VIDEODRIVER=offscreen.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config \
        libsdl2-dev libepoxy-dev libgl-dev libegl-dev libgles-dev \
        libgl1-mesa-dri libegl-mesa0 libssl-dev gdb procps \
    && rm -rf /var/lib/apt/lists/*
