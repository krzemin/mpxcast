FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture armhf && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf \
        bash \
        build-essential \
        ca-certificates \
        clang \
        cmake \
        gcc \
        git \
        libc6-dev-armhf-cross \
        libliquid-dev \
        libliquid-dev:armhf \
        libmicrohttpd-dev \
        libmicrohttpd-dev:armhf \
        librtlsdr-dev \
        librtlsdr-dev:armhf \
        pkg-config && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]
