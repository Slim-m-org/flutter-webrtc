#include "flutter_deep_filter.h"

#include <cmath>
#include <limits>

#ifdef SLIMM_HAS_DFN
extern "C" {
struct Dfn;
Dfn* slimm_dfn_create(float atten_lim_db, float min_db, float erb_db,
                      float df_db);
float slimm_dfn_process(Dfn* dfn, const float* input, float* output);
void slimm_dfn_free(Dfn* dfn);
}
#endif

namespace flutter_webrtc_plugin {

namespace {

// The model takes samples in [-1, 1]; the capture chain is on the int16 scale.
constexpr float kFullScale = 32768.0f;

}  // namespace

DeepFilter::Params DeepFilter::DefaultParams() {
  const float keep = std::numeric_limits<float>::quiet_NaN();
  return {keep, keep, keep, keep};
}

#ifdef SLIMM_HAS_DFN

struct DeepFilter::Impl {
  ~Impl() { slimm_dfn_free(dfn); }

  Dfn* dfn = nullptr;
  float in[kFrameSize];
  float out[kFrameSize];
};

bool DeepFilter::Compiled() { return true; }

std::unique_ptr<DeepFilter> DeepFilter::Load(const Params& params) {
  auto impl = std::make_unique<Impl>();
  impl->dfn = slimm_dfn_create(params.atten_lim_db, params.min_db,
                               params.erb_db, params.df_db);
  if (impl->dfn == nullptr) return nullptr;
  return std::unique_ptr<DeepFilter>(new DeepFilter(impl.release()));
}

bool DeepFilter::Process(const float* in, float* out) {
  for (int i = 0; i < kFrameSize; ++i) impl_->in[i] = in[i] / kFullScale;
  if (std::isnan(slimm_dfn_process(impl_->dfn, impl_->in, impl_->out))) {
    return false;
  }
  for (int i = 0; i < kFrameSize; ++i) {
    if (!std::isfinite(impl_->out[i])) return false;
  }
  for (int i = 0; i < kFrameSize; ++i) out[i] = impl_->out[i] * kFullScale;
  return true;
}

#else

struct DeepFilter::Impl {};

bool DeepFilter::Compiled() { return false; }

std::unique_ptr<DeepFilter> DeepFilter::Load(const Params& /*params*/) {
  return nullptr;
}

bool DeepFilter::Process(const float* /*in*/, float* /*out*/) { return false; }

#endif

DeepFilter::DeepFilter(Impl* impl) : impl_(impl) {}
DeepFilter::~DeepFilter() = default;

}  // namespace flutter_webrtc_plugin
