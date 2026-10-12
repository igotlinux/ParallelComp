# KernelGuard Reproducible Build & Verification Container
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

# Install core compilers, build tools, and dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-12 \
    g++-12 \
    clang-15 \
    clang++-15 \
    cmake \
    ninja-build \
    valgrind \
    libmpfr-dev \
    python3 \
    python3-pip \
    python3-yaml \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set up unprivileged user for security isolation
RUN useradd -m -s /bin/bash kguser

WORKDIR /workspace
COPY . /workspace
RUN chown -R kguser:kguser /workspace

USER kguser

# Build C++ harness and core library
RUN cmake -B build -G Ninja && cmake --build build
RUN pip3 install --no-cache-dir --user .

ENV PATH="/home/kguser/.local/bin:${PATH}"

CMD ["kg", "run", "--profile", "quick"]
