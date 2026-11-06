#include "portal_stream_watch.h"

#include <gio/gio.h>

#include <cstdio>
#include <mutex>

namespace flutter_webrtc_plugin {
namespace {

constexpr char kRequestInterface[] = "org.freedesktop.portal.Request";
constexpr char kScreenCastInterface[] = "org.freedesktop.portal.ScreenCast";
constexpr char kCreateSession[] = "CreateSession";
constexpr char kResponse[] = "Response";
// xdg-desktop-portal's SourceType bit for a monitor share.
constexpr guint32 kMonitorSource = 1;

std::mutex g_mutex;
bool g_installed = false;
std::optional<PortalStreamGeometry> g_last;

void Remember(GVariant* results) {
  GVariant* streams = g_variant_lookup_value(results, "streams",
                                             G_VARIANT_TYPE("a(ua{sv})"));
  if (streams == nullptr) {
    return;
  }
  if (g_variant_n_children(streams) > 0) {
    guint32 node = 0;
    GVariant* props = nullptr;
    g_variant_get_child(streams, 0, "(u@a{sv})", &node, &props);
    guint32 source_type = kMonitorSource;
    g_variant_lookup(props, "source_type", "u", &source_type);
    gint32 x = 0, y = 0, width = 0, height = 0;
    const bool placed = g_variant_lookup(props, "position", "(ii)", &x, &y);
    g_variant_lookup(props, "size", "(ii)", &width, &height);
    if (placed && source_type == kMonitorSource) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_last = PortalStreamGeometry{x, y, width, height};
    }
    g_variant_unref(props);
  }
  g_variant_unref(streams);
}

GDBusMessage* WatchStartReply(GDBusConnection*,
                              GDBusMessage* message,
                              gboolean incoming,
                              gpointer) {
  if (!incoming &&
      g_dbus_message_get_message_type(message) ==
          G_DBUS_MESSAGE_TYPE_METHOD_CALL &&
      g_strcmp0(g_dbus_message_get_interface(message), kScreenCastInterface) ==
          0 &&
      g_strcmp0(g_dbus_message_get_member(message), kCreateSession) == 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last.reset();
    return message;
  }
  if (!incoming ||
      g_dbus_message_get_message_type(message) != G_DBUS_MESSAGE_TYPE_SIGNAL ||
      g_strcmp0(g_dbus_message_get_interface(message), kRequestInterface) !=
          0 ||
      g_strcmp0(g_dbus_message_get_member(message), kResponse) != 0) {
    return message;
  }
  GVariant* body = g_dbus_message_get_body(message);
  if (body == nullptr ||
      !g_variant_is_of_type(body, G_VARIANT_TYPE("(ua{sv})"))) {
    return message;
  }
  guint32 response = 1;
  GVariant* results = nullptr;
  g_variant_get(body, "(u@a{sv})", &response, &results);
  if (response == 0) {
    Remember(results);
  }
  g_variant_unref(results);
  return message;
}

}  // namespace

void PortalStreamWatch::Arm() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_installed) {
    return;
  }
  GError* error = nullptr;
  // The shared connection libwebrtc's portal calls go over; held for the
  // life of the process, like the filter on it.
  GDBusConnection* connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (connection == nullptr) {
    fprintf(stderr, "flutter_webrtc: no session bus for the portal watch: %s\n",
            error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    return;
  }
  g_dbus_connection_add_filter(connection, WatchStartReply, nullptr, nullptr);
  g_installed = true;
}

std::optional<PortalStreamGeometry> PortalStreamWatch::Last() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last;
}

}  // namespace flutter_webrtc_plugin
