// gazebo-bridge: gz::msgs::Image -> GStreamer (H.264) -> RTSP
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glib.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <gz/msgs/image.pb.h>
#include <gz/transport.hh>

namespace {

struct Config {
  std::string camera_topic{"/camera/image"};
  int bitrate_kbps{2000};
  int fps{30};
  int gop{30};
  int out_width{0};   // 0 = use sensor size
  int out_height{0};
};

struct StreamGeometry {
  std::mutex mut;
  std::condition_variable cv;
  bool have_geometry{false};
  unsigned width{0};
  unsigned height{0};
  unsigned step{0};
  std::string gst_format; // "RGB" or "BGR"
};

struct AppSrcSlot {
  std::mutex mut;
  GstElement *appsrc{nullptr};
};

Config g_config;
StreamGeometry g_geo;
AppSrcSlot g_appsrc_slot;

std::atomic<bool> g_running{true};

int env_int(const char *name, int def) {
  const char *v = std::getenv(name);
  if (!v || !*v) {
    return def;
  }
  return std::atoi(v);
}

bool env_string(const char *name, std::string *out) {
  const char *v = std::getenv(name);
  if (!v || !*v) {
    return false;
  }
  *out = v;
  return true;
}

// Proto enum values: RGB_INT8 = 3, BGR_INT8 = 8 (gz/msgs/image.proto)
const char *pixel_format_to_gst(int fmt) {
  if (fmt == 3) {
    return "RGB";
  }
  if (fmt == 8) {
    return "BGR";
  }
  return nullptr;
}

// Pack rows into a tight buffer when step > width * channels.
void pack_image(const gz::msgs::Image &msg, unsigned channels, std::vector<uint8_t> *out) {
  const unsigned w = msg.width();
  const unsigned h = msg.height();
  const unsigned step = msg.step();
  const unsigned row_packed = w * channels;
  out->resize(static_cast<size_t>(row_packed) * h);
  const uint8_t *src = reinterpret_cast<const uint8_t *>(msg.data().data());
  uint8_t *dst = out->data();
  if (step == row_packed) {
    std::memcpy(dst, src, out->size());
    return;
  }
  for (unsigned y = 0; y < h; ++y) {
    std::memcpy(dst + static_cast<size_t>(y) * row_packed, src + static_cast<size_t>(y) * step,
                row_packed);
  }
}

void on_image(const gz::msgs::Image &msg) {
  if (!g_running.load(std::memory_order_relaxed)) {
    return;
  }

  const char *gst_fmt = pixel_format_to_gst(static_cast<int>(msg.pixel_format_type()));
  if (!gst_fmt) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
      std::cerr << "Unsupported pixel_format_type (only RGB_INT8 and BGR_INT8). Dropping frames.\n";
    }
    return;
  }

  const unsigned channels = 3u;
  if (msg.width() == 0 || msg.height() == 0 || msg.data().size() < msg.step() * msg.height()) {
    return;
  }

  std::vector<uint8_t> packed;
  pack_image(msg, channels, &packed);

  {
    std::lock_guard<std::mutex> lk(g_geo.mut);
    g_geo.width = msg.width();
    g_geo.height = msg.height();
    g_geo.step = msg.step();
    g_geo.gst_format = gst_fmt;
    g_geo.have_geometry = true;
  }
  g_geo.cv.notify_all();

  GstElement *app = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_appsrc_slot.mut);
    app = g_appsrc_slot.appsrc;
    if (app) {
      gst_object_ref(app);
    }
  }
  if (!app) {
    return;
  }

  GstBuffer *buf = gst_buffer_new_allocate(nullptr, packed.size(), nullptr);
  if (!buf) {
    gst_object_unref(app);
    return;
  }
  GstMapInfo map;
  if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
    gst_buffer_unref(buf);
    gst_object_unref(app);
    return;
  }
  std::memcpy(map.data, packed.data(), packed.size());
  gst_buffer_unmap(buf, &map);

  GST_BUFFER_DURATION(buf) = gst_util_uint64_scale_int(1, GST_SECOND, g_config.fps);

  GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(app), buf);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref(buf);
  }
  gst_object_unref(app);
}

void clear_appsrc() {
  std::lock_guard<std::mutex> lk(g_appsrc_slot.mut);
  if (g_appsrc_slot.appsrc) {
    gst_object_unref(g_appsrc_slot.appsrc);
    g_appsrc_slot.appsrc = nullptr;
  }
}

static void media_configure_impl(GstRTSPMediaFactory * /*factory*/, GstRTSPMedia *media) {
  GstElement *element = gst_rtsp_media_get_element(media);
  GstElement *appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");
  gst_object_unref(element);
  if (!appsrc) {
    std::cerr << "media-configure: appsrc mysrc not found\n";
    return;
  }

  int w = 0;
  int h = 0;
  int fps = g_config.fps;
  std::string fmt;
  {
    std::lock_guard<std::mutex> lk(g_geo.mut);
    w = static_cast<int>(g_geo.width);
    h = static_cast<int>(g_geo.height);
    fmt = g_geo.gst_format;
  }

  g_object_set(G_OBJECT(appsrc), "format", GST_FORMAT_TIME, "is-live", TRUE, "block", FALSE,
               "do-timestamp", TRUE, "max-buffers", 2, nullptr);

  GstCaps *caps =
      gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, fmt.c_str(), "width", G_TYPE_INT, w,
                          "height", G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION, fps, 1, nullptr);
  gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
  gst_caps_unref(caps);

  {
    std::lock_guard<std::mutex> lk(g_appsrc_slot.mut);
    if (g_appsrc_slot.appsrc) {
      gst_object_unref(g_appsrc_slot.appsrc);
    }
    g_appsrc_slot.appsrc = GST_ELEMENT(gst_object_ref(appsrc));
  }

  gst_object_unref(appsrc);
}

static void media_configure_cb(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer) {
  media_configure_impl(factory, media);
}

static gboolean sig_quit_cb(gpointer user_data) {
  g_running.store(false, std::memory_order_relaxed);
  clear_appsrc();
  g_main_loop_quit(static_cast<GMainLoop *>(user_data));
  return G_SOURCE_REMOVE;
}

std::string build_launch_line() {
  const int fps = g_config.fps;
  const int br = g_config.bitrate_kbps;
  const int gop = g_config.gop;
  std::string mid = "videoconvert ! ";
  if (g_config.out_width > 0 && g_config.out_height > 0) {
    mid += "videoscale ! video/x-raw,width=" + std::to_string(g_config.out_width) + ",height=" +
           std::to_string(g_config.out_height) + " ! ";
  }
  // key-int-max: max distance between keyframes in frames
  std::string enc = "x264enc tune=zerolatency speed-preset=ultrafast bitrate=" + std::to_string(br) +
                    " key-int-max=" + std::to_string(gop) + " ! "
                    "video/x-h264,profile=baseline ! "
                    "rtph264pay name=pay0 pt=96";
  return std::string("( appsrc name=mysrc ! queue max-size-buffers=2 leaky=downstream ! ") + mid + enc +
         " )";
}

} // namespace

int main(int argc, char **argv) {
  (void)argv;
  if (env_string("CAMERA_TOPIC", &g_config.camera_topic)) {
    // set
  }
  g_config.bitrate_kbps = env_int("BITRATE", 2000);
  g_config.fps = env_int("FPS", 30);
  g_config.gop = env_int("GOP", 30);
  if (g_config.gop < 1) {
    g_config.gop = 30;
  }
  const char *w_env = std::getenv("WIDTH");
  const char *h_env = std::getenv("HEIGHT");
  if (w_env && h_env && *w_env && *h_env) {
    g_config.out_width = std::atoi(w_env);
    g_config.out_height = std::atoi(h_env);
  }

  gst_init(&argc, &argv);

  gz::transport::Node node;
  if (!node.Subscribe<gz::msgs::Image>(g_config.camera_topic,
                                       [](const gz::msgs::Image &img) { on_image(img); })) {
    std::cerr << "Failed to subscribe to topic: " << g_config.camera_topic << "\n";
    return 1;
  }

  std::cout << "Waiting for first gz::msgs::Image on " << g_config.camera_topic << " ...\n";
  {
    std::unique_lock<std::mutex> lk(g_geo.mut);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!g_geo.have_geometry && std::chrono::steady_clock::now() < deadline) {
      lk.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      lk.lock();
    }
    if (!g_geo.have_geometry) {
      std::cerr << "Timeout: no image on topic. Start gz-sim camera first or set CAMERA_TOPIC.\n";
      return 1;
    }
  }

  GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

  GstRTSPServer *server = gst_rtsp_server_new();
  gst_rtsp_server_set_service(server, "8554");

  GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  std::string launch = build_launch_line();
  gst_rtsp_media_factory_set_launch(factory, launch.c_str());
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_signal_connect(factory, "media-configure", G_CALLBACK(media_configure_cb), nullptr);

  gst_rtsp_mount_points_add_factory(mounts, "/stream", factory);
  g_object_unref(mounts);

  gst_rtsp_server_attach(server, nullptr);

  std::cout << "RTSP stream: rtsp://0.0.0.0:8554/stream\n";

  g_unix_signal_add(SIGINT, sig_quit_cb, loop);
  g_unix_signal_add(SIGTERM, sig_quit_cb, loop);

  g_main_loop_run(loop);

  clear_appsrc();
  g_object_unref(server);
  g_main_loop_unref(loop);
  gst_deinit();
  return 0;
}
