#import "CaptureChainProcessing.h"

#include "flutter_capture_chain_api.h"

@implementation CaptureChainProcessing

- (void)audioProcessingInitializeWithSampleRate:(size_t)sampleRateHz channels:(size_t)channels {
  slimm_chain_initialize((int)sampleRateHz, (int)channels);
}

// The first channel is libwebrtc's own buffer, so the chain edits what is sent in place.
- (void)audioProcessingProcess:(RTC_OBJC_TYPE(RTCAudioBuffer) *)audioBuffer {
  if (audioBuffer.channels == 0 || audioBuffer.frames == 0) return;
  slimm_chain_process([audioBuffer rawBufferForChannel:0], (int)audioBuffer.frames);
}

- (void)audioProcessingRelease {
}

+ (void)configure:(NSDictionary*)args {
  NSNumber* threshold = args[@"thresholdDb"];
  slimm_chain_configure([args[@"enabled"] boolValue],
                        threshold != nil ? [threshold floatValue] : -50.0f,
                        [args[@"dynamics"] boolValue],
                        [args[@"levelAfterSuppression"] boolValue],
                        [args[@"strongNoiseSuppression"] boolValue]);
}

+ (NSDictionary*)read {
  return @{
    @"levelDb" : @(slimm_chain_take_peak_db()),
    @"voice" : @(slimm_chain_voice() != 0),
    @"dynamics" : @(slimm_chain_dynamics() != 0),
    @"levelerGainDb" : @(slimm_chain_leveler_gain_db()),
    @"strongNoiseSuppression" : @(slimm_chain_strong_active() != 0),
    @"strongestNoiseSuppression" : @NO,
  };
}

@end
