# gazebo-bridge

Small C++ **boundary adapter**: subscribe to a **Gazebo Sim** camera image on **gz-transport** (`gz::msgs::Image`), encode **H.264** with **GStreamer** (low-latency `x264enc`), and serve **RTSP** at `rtsp://0.0.0.0:8554/stream` for **VLC** or **ffplay**. No telemetry, no drone logic—swap the simulator or a real camera without changing viewers.

## Requirements

- **Gazebo Sim** with a camera publishing `gz::msgs::Image` (RGB8 or BGR8).
- **GStreamer** + **gst-rtsp-server** development packages.
- **gz-transport** matching your distro (Harmonic / Ionic package names differ by SONAME).

## Discover the camera topic

```bash
gz topic -l | grep -i image
```

Set `CAMERA_TOPIC` to the full topic name your sensor uses.

## Environment variables

| Variable       | Default           | Description |
|----------------|-------------------|-------------|
| `CAMERA_TOPIC` | `/camera/image`   | gz-transport topic for `gz::msgs::Image`. |
| `BITRATE`      | `2000`            | H.264 bitrate (kbps). |
| `FPS`          | `30`              | Caps / buffer duration hint (source frames may still be dropped). |
| `GOP`          | `30`              | x264 `key-int-max`. |
| `WIDTH`        | _(unset)_         | If set with `HEIGHT`, `videoscale` to this size. |
| `HEIGHT`       | _(unset)_         | If set with `WIDTH`, `videoscale` to this size. |

Startup configuration only (no hot reload).

## Native build (Ubuntu)

**gz-transport** is published on [packages.osrfoundation.org](https://gazebosim.org/docs/latest/install_ubuntu/) (add the Gazebo apt repo if `libgz-transport*-dev` is not found). Then:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-ugly libgstrtspserver-1.0-dev \
  libgz-transport14-dev \
  libcppzmq-dev
```

If `libcppzmq-dev` is unavailable on your arch, install `libzmq3-dev` and install the header `zmq.hpp` from [cppzmq](https://github.com/zeromq/cppzmq) (see [`Dockerfile`](Dockerfile)).

If CMake cannot find gz-transport, install the dev package that matches your system and pass the pkg-config module name:

```bash
cmake -S . -B build -DGZ_TRANSPORT_PKG=gz-transport15
cmake --build build -j$(nproc)
```

## Run

1. Start **gz-sim** (or any publisher) so `CAMERA_TOPIC` is producing images **before** the bridge starts (the binary waits up to 120s for the first frame).
2. Run:

```bash
./build/gazebo_bridge
```

3. View:

```bash
ffplay -fflags nobuffer -flags low_delay rtsp://127.0.0.1:8554/stream
```

If RTP over UDP is blocked, use TCP:

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/stream
```

**Security:** RTSP is **unauthenticated** and bound to `0.0.0.0`. Use only on trusted networks or behind a firewall/VPN.

## Docker

```bash
docker build -t gazebo-bridge .
docker run --rm -e CAMERA_TOPIC=/your/topic -p 8554:8554 --network host gazebo-bridge
```

`--network host` is often needed so the container shares the same gz-transport discovery as gz-sim on the host.

## End-to-end test: Docker Compose (gz-sim + bridge + viewer)

This repo ships a **minimal camera world** ([`worlds/test_camera.sdf`](worlds/test_camera.sdf)) and Compose files so you can validate the full path without installing gz-sim on the host.

### Recommended: single container ([`compose.e2e.yaml`](compose.e2e.yaml))

**gz-transport discovery often fails between two containers on Docker Desktop (Mac)**. The reliable path is **one container** running gz-sim and the bridge in the **same network namespace** ([`Dockerfile.e2e`](Dockerfile.e2e) + [`docker/e2e-entrypoint.sh`](docker/e2e-entrypoint.sh)).

```bash
docker compose -f compose.e2e.yaml up --build
```

### Two services ([`compose.yaml`](compose.yaml))

On **Linux** hosts where multicast discovery works across the Compose network, you can use sim + bridge as separate services:

1. **Start sim + bridge** (first build can take several minutes; image pulls + `gz-harmonic` are large):

   ```bash
   docker compose up --build
   ```

   - **Sim** runs `gz sim -r -s /worlds/test_camera.sdf` (headless server).
   - **Bridge** waits until the camera image topic exists, then listens on **RTSP** port **8554**.

2. **View the stream** on your machine (host needs **ffplay** from FFmpeg):

   ```bash
   chmod +x scripts/view-stream.sh
   ./scripts/view-stream.sh
   ```

   TCP fallback if UDP RTP is blocked:

   ```bash
   RTSP_TRANSPORT=tcp ./scripts/view-stream.sh
   ```

   Or manually:

   ```bash
   ffplay -fflags nobuffer -flags low_delay rtsp://127.0.0.1:8554/stream
   ```

**Notes**

- **Harmonic** publishes this world’s camera as **`/camera`** (`gz.msgs.Image`). Use `gz topic -l` if you change the SDF.
- Compose builds the bridge against **gz-transport13** to match **Gazebo Harmonic** (`gz-harmonic` in `Dockerfile.sim`). If you switch the sim image to another collection, update `GZ_TRANSPORT_DEV_PKG` / `GZ_TRANSPORT_PC_MODULE` in [`compose.yaml`](compose.yaml) / [`Dockerfile.e2e`](Dockerfile.e2e) to match.
- Headless **Ogre2** rendering in Docker can fail on some hosts (GPU drivers, EGL). If the sim container exits or never becomes healthy, run gz-sim **natively** and only run the bridge container with `--network host`, or debug with `docker compose logs sim`.
- The camera topic used in Compose is **`/camera`** (Harmonic maps the SDF camera topic that way; confirm with `gz topic -l` / `gz topic -i -t /camera`).

## Behavior notes

- **Timestamps:** wall-time / pipeline live mode (`do-timestamp` on `appsrc`); simulation time is not used for RTP.
- **Backpressure:** bounded `appsrc` + leaky queue; slow encode drops frames instead of growing memory.
- **Pixel formats:** **RGB_INT8** and **BGR_INT8** only; other formats log once and are ignored.

## License

MIT—see [LICENSE](LICENSE).
