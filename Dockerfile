# Ubuntu 24.04 + Gazebo Sim stack (gz-transport) + GStreamer RTSP.
# Adjust libgz-transport*-dev if your distro pins a different SONAME (see CMakeLists.txt / README).
FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstrtspserver-1.0-dev \
        libgz-transport14-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY CMakeLists.txt main.cpp ./
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

ENV CAMERA_TOPIC=/camera/image
EXPOSE 8554

# Requires a gz-sim camera publishing gz::msgs::Image on CAMERA_TOPIC (often via host network).
CMD ["./build/gazebo_bridge"]
