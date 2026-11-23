#import <Foundation/Foundation.h>
#import "AudioProcessingAdapter.h"

// Runs the desktop capture chain (Strong, the input gate and the dynamics,
// in common/cpp) on every 10 ms microphone frame, after libwebrtc's own
// processing. Answers the same setInputGate and getInputGate calls the
// desktop plugin does. iOS only: macOS is not built with the chain.
@interface CaptureChainProcessing : NSObject <ExternalAudioProcessingDelegate>

// Takes the setInputGate arguments; Strongest is desktop only.
+ (void)configure:(nonnull NSDictionary*)args;

// The getInputGate reading; the level is the loudest frame since the last call.
+ (nonnull NSDictionary*)read;

@end
