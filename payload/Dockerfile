# Reproducible build environment for the payload.
#   docker build -t vstghost-payload payload
#   docker run --rm -v "$PWD:/src" -w /src vstghost-payload payload/build.sh
FROM ubuntu:24.04
COPY setup-ubuntu.sh /tmp/setup-ubuntu.sh
RUN bash /tmp/setup-ubuntu.sh && rm -rf /var/lib/apt/lists/*
