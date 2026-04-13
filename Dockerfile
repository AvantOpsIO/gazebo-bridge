# Ubuntu 24.04 + Gazebo Sim stack (gz-transport) + GStreamer RTSP.
# Adjust libgz-transport*-dev if your distro pins a different SONAME (see CMakeLists.txt / README).
FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive

# Pin to the gz-transport SONAME that matches your Gazebo collection (Harmonic → 13, Ionic → 14, etc.).
ARG GZ_TRANSPORT_DEV_PKG=libgz-transport14-dev
ARG GZ_TRANSPORT_PC_MODULE=gz-transport14

# Gazebo / gz packages are on packages.osrfoundation.org (not always in default Ubuntu universe).
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gnupg \
        lsb-release \
    && curl -fsSL https://packages.osrfoundation.org/gazebo.gpg \
        -o /usr/share/keyrings/pkgs-osrf-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] https://packages.osrfoundation.org/gazebo/ubuntu-stable $(lsb_release -cs) main" \
        > /etc/apt/sources.list.d/gazebo-stable.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstrtspserver-1.0-dev \
        "${GZ_TRANSPORT_DEV_PKG}" \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY CMakeLists.txt main.cpp ./
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGZ_TRANSPORT_PKG="${GZ_TRANSPORT_PC_MODULE}" \
    && cmake --build build -j"$(nproc)"

ENV CAMERA_TOPIC=/camera/image
EXPOSE 8554

# Requires a gz-sim camera publishing gz::msgs::Image on CAMERA_TOPIC (often via host network).
CMD ["./build/gazebo_bridge"]
