# Build environment for the payload: Ubuntu 26.04 with LLVM 18 (the payload SDK's toolchain wraps clang and lld).
FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl unzip make clang-18 lld-18 llvm-18 python3 \
    && ln -s /usr/bin/clang-18 /usr/local/bin/clang && ln -s /usr/bin/ld.lld-18 /usr/local/bin/ld.lld \
    && rm -rf /var/lib/apt/lists/*
