#import "CaptureChainProcessing.h"

#include "flutter_capture_chain_api.h"
#include "flutter_chain_glue.h"

@implementation CaptureChainProcessing

- (void)audioProcessingInitializeWithSampleRate:(size_t)sampleRateHz channels:(size_t)channels {
  slimm_chain_initialize(flutter_webrtc_plugin::ClampToInt(sampleRateHz),
                         flutter_webrtc_plugin::ClampToInt(channels));
}

// The first channel is libwebrtc's own buffer, so the chain edits what is sent in place.
- (void)audioProcessingProcess:(RTC_OBJC_TYPE(RTCAudioBuffer) *)audioBuffer {
  if (!flutter_webrtc_plugin::AudioBufferUsable(audioBuffer.channels, audioBuffer.frames)) return;
  slimm_chain_process([audioBuffer rawBufferForChannel:0], (int)audioBuffer.frames);
}

- (void)audioProcessingRelease {
}

+ (void)configure:(NSDictionary*)args {
  NSNumber* threshold = args[@"thresholdDb"];
  slimm_chain_configure([args[@"enabled"] boolValue],
                        flutter_webrtc_plugin::GateThresholdOrDefault(threshold != nil, [threshold floatValue]),
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
