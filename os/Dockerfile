# The Linux box Buildroot runs in. Buildroot doesn't support macOS
# as a build host, so the toolchain and the Nook build happen here.
# Native arm64 on the Mac, so there's no emulation slowing it down.
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
	bc bzip2 ca-certificates cpio file g++ gcc git make patch perl \
	python3 rsync unzip wget xz-utils \
	&& rm -rf /var/lib/apt/lists/*

# Buildroot refuses to run as root.
RUN useradd -m -u 1000 builder
USER builder
WORKDIR /home/builder
