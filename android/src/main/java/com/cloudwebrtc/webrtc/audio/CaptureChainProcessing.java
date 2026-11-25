package com.cloudwebrtc.webrtc.audio;

import android.util.Log;

import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

/**
 * Runs the desktop capture chain (Strong, the input gate and the dynamics, in
 * common/cpp) on every 10 ms microphone frame, after libwebrtc's own echo
 * cancellation, noise suppression and auto gain. Answers the same
 * setInputGate and getInputGate calls the desktop plugin does.
 */
public final class CaptureChainProcessing implements AudioProcessingAdapter.ExternalAudioFrameProcessing {
    private static final String TAG = "CaptureChain";
    private static final boolean LOADED = load();

    private static boolean load() {
        try {
            System.loadLibrary("slimm_capture_chain");
            return true;
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "No capture chain in this build; the mic is sent as libwebrtc processed it", error);
            return false;
        }
    }

    /** Whether this build carries the native chain at all. */
    public static boolean available() {
        return LOADED;
    }

    @Override
    public void initialize(int sampleRateHz, int numChannels) {
        if (LOADED) nativeInitialize(sampleRateHz, numChannels);
    }

    @Override
    public void reset(int newRate) {
        if (LOADED) nativeInitialize(newRate, 1);
    }

    @Override
    public void process(int numBands, int numFrames, ByteBuffer buffer) {
        if (LOADED) nativeProcess(buffer, numFrames);
    }

    /** Takes the setInputGate arguments; Strongest is desktop only. */
    public static void configure(Map<String, Object> args) {
        if (!LOADED) throw new IllegalStateException("no capture chain in this build");
        Object threshold = args.get("thresholdDb");
        nativeConfigure(
                Boolean.TRUE.equals(args.get("enabled")),
                threshold instanceof Number ? ((Number) threshold).floatValue() : -50.0f,
                Boolean.TRUE.equals(args.get("dynamics")),
                Boolean.TRUE.equals(args.get("levelAfterSuppression")),
                Boolean.TRUE.equals(args.get("strongNoiseSuppression")));
    }

    /** The getInputGate reading; the level is the loudest frame since the last call. */
    public static Map<String, Object> read() {
        if (!LOADED) throw new IllegalStateException("no capture chain in this build");
        float[] values = nativeRead();
        Map<String, Object> map = new HashMap<>();
        map.put("levelDb", (double) values[0]);
        map.put("voice", values[1] != 0.0f);
        map.put("dynamics", values[2] != 0.0f);
        map.put("levelerGainDb", (double) values[3]);
        map.put("strongNoiseSuppression", values[4] != 0.0f);
        map.put("strongestNoiseSuppression", false);
        map.put("rejectedFrames", (long) values[5]);
        return map;
    }

    private static native void nativeInitialize(int sampleRateHz, int numChannels);

    private static native void nativeProcess(ByteBuffer buffer, int numFrames);

    private static native void nativeConfigure(
            boolean gateEnabled, float thresholdDb, boolean dynamics,
            boolean levelAfterSuppression, boolean strong);

    private static native float[] nativeRead();
}
