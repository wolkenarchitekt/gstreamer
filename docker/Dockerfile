ARG GSTREAMER_VERSION=1.24.0
ARG UBUNTU_RELEASE=24.04
ARG GSTREAMER_PLUGINS=gst-plugins-base,gst-plugins-good,gst-plugins-bad,gst-plugins-ugly,gst-plugins-rs

FROM ubuntu:${UBUNTU_RELEASE}

ARG GSTREAMER_VERSION
ARG UBUNTU_RELEASE
ARG GSTREAMER_PLUGINS

# https://gstreamer.freedesktop.org/documentation/installing/on-linux.html?gi-language=c#install-gstreamer-on-ubuntu-or-debian
RUN apt-get update && \
    apt-get install -y \
    bison \
    cargo \
    curl \
    flex \
    git \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-nice \
    meson \
    ninja-build \
    openssl \
    pkg-config \
    python3-pip \
    libcairo-gobject2 \
    libcairo2-dev \
    libglib2.0-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libpango1.0-dev \
    libssl-dev \
    npm \
    rustc && \
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*

# Build and install gstreamer
WORKDIR /src/gstreamer
RUN cargo install cargo-c && ln -s /root/.cargo/bin/cargo-cbuild /usr/bin/cargo-cbuild
RUN git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git /src/gstreamer --depth 1 --branch ${GSTREAMER_VERSION}
RUN meson setup -Dgood=enabled -Drs=enabled -Dlibsoup:sysprof=disabled -Dcustom_subprojects="${GSTREAMER_PLUGINS}" build \
    && ninja -C build \
    && meson install -C build

## Build rswebrtc
## https://gstreamer.freedesktop.org/documentation/rswebrtc/index.html?gi-language=c#building
RUN cd /src/gstreamer/subprojects/gst-plugins-rs/net/webrtc && cargo build

# Build rswebrtc API
RUN cd /src/gstreamer/subprojects/gst-plugins-rs/net/webrtc/gstwebrtc-api && npm install
