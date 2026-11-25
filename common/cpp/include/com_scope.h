#ifndef FLUTTER_WEBRTC_PLUGIN_COM_SCOPE_H_
#define FLUTTER_WEBRTC_PLUGIN_COM_SCOPE_H_

namespace flutter_webrtc_plugin {

// Holds a thread's COM apartment for a scope. `Api::Enter()` returns whether
// the thread joined one, and only a joined apartment is left again.
template <typename Api>
class ComScope {
 public:
  ComScope() : joined_(Api::Enter()) {}
  ~ComScope() {
    if (joined_) Api::Leave();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

  bool joined() const { return joined_; }

 private:
  bool joined_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_COM_SCOPE_H_
