#include "portal_session_guard.h"

#include <gio/gio.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

namespace flutter_webrtc_plugin {
namespace {

constexpr char kScreenCastInterface[] = "org.freedesktop.portal.ScreenCast";
constexpr char kCreateSession[] = "CreateSession";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr guint kSettleTimeoutMs = 1500;

std::mutex g_mutex;
bool g_armed = false;
bool g_listed_once = false;
guint g_filter_id = 0;
GDBusConnection* g_connection = nullptr;
std::function<void()> g_waiting;

bool UnderWayland() {
  const char* session_type = std::getenv("XDG_SESSION_TYPE");
  return session_type != nullptr && std::string(session_type) == "wayland";
}

// Disarms for good: after this nothing can be dropped, and the filter itself
// is gone, so a late list call can never take out the capturer's.
std::function<void()> Settle() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_armed = false;
  if (g_filter_id != 0) {
    g_dbus_connection_remove_filter(g_connection, g_filter_id);
    g_filter_id = 0;
    g_clear_object(&g_connection);
  }
  return std::exchange(g_waiting, nullptr);
}

gboolean RunWaiting(gpointer) {
  if (auto done = Settle()) {
    done();
  }
  return G_SOURCE_REMOVE;
}

GDBusMessage* DropListSession(GDBusConnection*,
                              GDBusMessage* message,
                              gboolean incoming,
                              gpointer) {
  if (incoming ||
      g_dbus_message_get_message_type(message) !=
          G_DBUS_MESSAGE_TYPE_METHOD_CALL ||
      g_strcmp0(g_dbus_message_get_interface(message), kScreenCastInterface) !=
          0 ||
      g_strcmp0(g_dbus_message_get_member(message), kCreateSession) != 0 ||
      g_strcmp0(g_dbus_message_get_path(message), kPortalPath) != 0) {
    return message;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_armed) {
      return message;
    }
    g_armed = false;
  }
  g_idle_add(RunWaiting, nullptr);
  g_object_unref(message);
  return nullptr;
}

bool InstallFilter() {
  GError* error = nullptr;
  GDBusConnection* connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (connection == nullptr) {
    fprintf(stderr, "flutter_webrtc: no session bus for the portal guard: %s\n",
            error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    return false;
  }
  g_filter_id = g_dbus_connection_add_filter(connection, DropListSession,
                                             nullptr, nullptr);
  g_connection = connection;
  return true;
}

}  // namespace

void PortalSessionGuard::Begin(bool swallow) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_listed_once) {
    return;
  }
  g_listed_once = true;
  if (swallow && UnderWayland() && InstallFilter()) {
    g_armed = true;
  }
}

void PortalSessionGuard::Disarm() {
  Settle();
}

void PortalSessionGuard::WhenSettled(std::function<void()> done) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_armed) {
      g_waiting = std::move(done);
      g_timeout_add(kSettleTimeoutMs, RunWaiting, nullptr);
      return;
    }
  }
  done();
}

}  // namespace flutter_webrtc_plugin
