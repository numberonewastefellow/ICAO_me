# =============================================================================
# OFIQ (Open Source Face Image Quality) - ISO/IEC 29794-5 reference impl
# Upstream:    https://github.com/BSI-OFIQ/OFIQ-Project (tag v1.1.2, Feb 2025)
# Vendored at: third_party/OFIQ-Project (locally pinned snapshot — no git
#              clone at build time, so the build is reproducible regardless
#              of upstream branch movement).
# Built per upstream BUILD.md spec for Ubuntu 22.04 / GCC 11.4 / Conan 2.18.1
# =============================================================================

# --------- Stage 1: builder ---------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        python3 \
        python3-pip \
        curl \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# OFIQ requires CMake >= 3.26; Ubuntu 22.04 ships 3.22. Conan must be 2.18.1.
RUN pip3 install --no-cache-dir cmake==3.28.3 conan==2.18.1

RUN conan profile detect --force

# Copy the vendored OFIQ source from third_party/ instead of cloning. This
# guarantees byte-for-byte reproducibility regardless of upstream changes.
COPY third_party/OFIQ-Project /opt/OFIQ-Project

# Copy locally-vendored model + conformance image zips. The CMakeLists.txt
# in third_party/OFIQ-Project has been patched to prefer file:///opt/downloads/
# when these files are present, so models never come from the internet.
# (Conan packages and ONNX Runtime tarball still come from upstream — they're
# library binaries and acceptable per the project's vendoring policy.)
COPY third_party/downloads /opt/downloads

# Strip CRLF line endings from any shell scripts (Windows hosts that cloned
# OFIQ via Git may have converted them via core.autocrlf=true).
RUN find /opt/OFIQ-Project -name '*.sh' -exec sed -i 's/\r$//' {} +

WORKDIR /opt/OFIQ-Project/scripts
RUN sh build.sh

# --------- Stage 2: slim runtime ---------
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libgomp1 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/OFIQ-Project/install_x86_64_linux /opt/ofiq
COPY --from=builder /opt/OFIQ-Project/data /opt/ofiq/data

ENV LD_LIBRARY_PATH=/opt/ofiq/Release/lib:/opt/ofiq/Release/bin

WORKDIR /work
ENTRYPOINT ["/opt/ofiq/Release/bin/OFIQSampleApp"]
CMD ["--help"]
