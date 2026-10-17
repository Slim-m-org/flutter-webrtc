#ifndef TEST_STUB_RTC_AUDIO_PROCESSING_H_
#define TEST_STUB_RTC_AUDIO_PROCESSING_H_

// Just the CustomProcessing interface, so the native tests build without
// downloading libwebrtc.
namespace libwebrtc {
class RTCAudioProcessing {
 public:
  class CustomProcessing {
   public:
    virtual void Initialize(int sample_rate_hz, int num_channels) = 0;
    virtual void Process(int num_bands, int num_frames, int buffer_size,
                         float* buffer) = 0;
    virtual void Reset(int new_rate) = 0;
    virtual void Release() = 0;

   protected:
    virtual ~CustomProcessing() {}
  };
};
}  // namespace libwebrtc

#endif  // TEST_STUB_RTC_AUDIO_PROCESSING_H_
