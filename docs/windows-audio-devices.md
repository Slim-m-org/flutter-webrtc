# Windows audio devices

How this fork opens Windows audio devices, what that means for ducking, Bluetooth headsets and headset buttons, and what is still open.
Audited 2026-10-17 against the prebuilt `libwebrtc.m144.7559.09` that `third_party/libwebrtc_version.ini` pins.

## Which audio device module the prebuilt uses

The prebuilt is built from `webrtc-sdk/libwebrtc`.
Its factory creates the module with `webrtc::CreateAudioDeviceModule(env_, kPlatformDefaultAudio, false)` (`src/rtc_peerconnection_factory_impl.cc`, `CreateAudioDeviceModule_w`).
On Windows that selects the legacy `AudioDeviceWindowsCore` (`modules/audio_device/audio_device_impl.cc` lines 193-198), not the newer `CoreAudioBase` module that `kWindowsCoreAudio2` would select.

The shipped `libwebrtc.dll` agrees.
It contains the log strings "Attempting to use the Windows Core Audio APIs" and "Windows Core Audio APIs will be utilized" (legacy module), and none of "SetClientProperties", "CoreAudioBase" or "AudioCategory" (the new module).
This was checked by searching the dll inside `libwebrtc-win-x64-release.zip`.

The line numbers below are from the `m144_release` branch of `webrtc-sdk/webrtc`, which is the nearest public source for that tag.
If the pin changes, recheck them.

## Stream category and ducking

The `AudioCategory_Communications` flag that upstream sets in `core_audio_utility_win.cc` (`SetClientProperties`, line 909) belongs to the new module, which this build does not use.
The legacy module never calls `SetClientProperties` at all (no match in `audio_device_core_win.cc`).
Its render stream is created with `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` and a null session GUID (lines 1966-1973), its capture stream with `EVENTCALLBACK | NOPERSIST` (lines 2253-2262).
Both are therefore the default category, "Other".

So in this build ducking cannot come from the stream category.
It comes from the endpoint role.
The legacy module starts with `_inputDevice` and `_outputDevice` set to `kDefaultCommunicationDevice` (`audio_device_core_win.cc` lines 384-385), and nothing in `media/engine/webrtc_voice_engine.cc` changes that.
Opening the default communications endpoint is what makes Windows treat the call as a communications stream and duck other apps.

The fork avoids that by selecting an endpoint by list index, which bypasses roles entirely:

- `common/cpp/src/flutter_webrtc_base.cc` lines 26-33 select the default console playout and recording device when the plugin starts.
- `common/cpp/src/flutter_media_stream.cc` lines 262-290 do the same in `getUserMedia` when no device id was given.
- `windows/default_audio_endpoint.cc` line 32 resolves the id with `GetDefaultAudioEndpoint(..., eConsole, ...)`, and `FlutterWebRTCBase::DefaultAudioDeviceIndex` matches it to a list index by endpoint id.
- With an index set, `InitPlayout` and `InitMicrophone` read the device from the endpoint collection and never ask for a role (`audio_device_core_win.cc` `InitSpeaker` lines 670-672 and `InitMicrophone` lines 742-744).

Can the fork change the category?
No, and it does not need to.
The `IAudioClient` is created and kept inside the prebuilt module (`_ptrClientOut`, `_ptrClientIn`), so the plugin has no handle to call `IAudioClient2::SetClientProperties` on.
Changing it would mean rebuilding libwebrtc, but the category is already "Other".

The ducking guarantee therefore rests on one thing: an index must be selected before the first stream opens.
It is not, if `DefaultAudioDeviceIndex` returns -1.
That happens when no endpoint is active at startup, or the enumerator fails.
The module then stays on the communications role until the app selects a device by id.
A fallback to a console endpoint is not possible from the plugin, since the module offers no console-role call other than `kDefaultDevice` through the enum overload, which `RTCAudioDevice` does not expose.

## Ducking opt-out

On top of avoiding the communications role, the plugin opts out of the system ducking experience (`windows/audio_ducking_opt_out.cc`).
It calls `IAudioSessionControl2::SetDuckingPreference(TRUE)` on the process default session (null session GUID) of every active render and capture endpoint.
Sessions belong to an endpoint, so the call is made once per endpoint and repeated for the whole list.
Microsoft recommends setting the preference during stream creation and allows changing it later, so it is applied at plugin start, in `getUserMedia` before the audio source is created, when an input or output is selected, and on every device change (which covers a newly plugged headset).
The controls are kept alive because the default session only exists while something references it, and the module opens its stream afterwards.
Every failure is logged with `OutputDebugString` and ignored.

Limits:

- It relies on the prebuilt module creating its streams in the default session, as audited above. If that module ever used a named session GUID, this opt-out would stop covering it.
- Windows may still apply the communications stream experience if the module opens the `eCommunications` role (open issue 3), but the opt-out also covers that endpoint.
- It affects only this process. It does not change the user's Windows "reduce by 80%" setting.
- A caller thread that is already in a single threaded apartment gets its own apartment, so the held controls then live and die with that thread.

## Capture and render roles

| Direction | Role used | Where |
| --- | --- | --- |
| Playout, index selected (normal path) | none, endpoint by index (the console default at startup) | `audio_device_core_win.cc` 670-672 |
| Capture, index selected (normal path) | none, endpoint by index | `audio_device_core_win.cc` 742-744 |
| Either, nothing selected | `eCommunications` | `audio_device_core_win.cc` 384-385, 674-677, 746-749 |
| Screen share application loopback | `eRender` / `eConsole` for the mix format only | `windows/application_loopback_capturer.cc` 342 |

The built-in DMO echo canceller (`_builtInAecEnabled`) is off by default (`audio_device_core_win.cc` line 354).
It would pick roles at lines 3444-3457, but is not used.

## Bluetooth headsets and the hands-free profile

Windows exposes a Bluetooth headset as two endpoint sets: "Headphones" (A2DP stereo) and "Headset" (Hands-Free, HFP, mono 8 or 16 kHz).
Opening any stream on a Hands-Free endpoint switches the headset to HFP, and A2DP stops while it is active.

- The fork never chooses a device on its own except the Windows default console device, so it opens an HFP endpoint only when that is the user's default or the user picked it.
- A headset that is the Windows default for both directions, or a user who picks the headset microphone, therefore gets HFP quality for as long as the mic is open. This is how Windows and the headset work and not something the plugin can avoid from user space.
- If the user pairs the headset's A2DP output with another microphone, nothing flips.
- HFP captures at 16 kHz or lower, which is exactly where Strong noise suppression previously did nothing. It now resamples (`flutter_noise_suppressor.cc`, `flutter_rate_converter.cc`).

## Headset buttons and mute

The plugin and the module do not register any HID, media-key or hot-key handling, and do not take exclusive mode (`AUDCLNT_SHAREMODE_SHARED` at lines 1967 and 2254).
Headset call and volume buttons are handled by the driver and Windows, and nothing in the plugin captures or consumes them.

What the module does touch:

- It activates `IAudioEndpointVolume` on the capture endpoint for mic volume and mute (`audio_device_core_win.cc` lines 760-765, 1059-1104, 1295). The plugin exposes `SetMicrophoneVolume` through `RTCAudioDevice`; nothing in the plugin sets the endpoint mute.
- A hardware mute on the headset or the Windows mic mute toggle silences the endpoint. The module does not register `IAudioEndpointVolumeCallback`, so the app is not told and shows the mic as live while nobody hears it.

## Open issues

Found by reading the code; none is fixed here because the fix is either outside the plugin or not small and safe.

1. Endpoint indexes go stale on hot plug. With an index selected the module re-reads the collection at each `InitPlayout` / `InitMicrophone` (`audio_device_core_win.cc` lines 670-672 and 742-744), so plugging in or removing a device can silently point the stored index at a different endpoint. The app should reselect by id on every device change. `RTCAudioDevice::OnDeviceChange` exists for this.
2. The plugin does not follow a changed Windows default device. The default console device is resolved once at startup and again only when `getUserMedia` runs without ids.
3. Falling back to the communications role (and therefore ducking) when no default console endpoint could be resolved at startup, described above.
4. Hardware mute is invisible to the app (see above). The level meter reads about -100 dB when it happens, which the app could use to warn the user.
5. Not established: whether libwebrtc's analog gain control moves the Windows microphone level through `SetMicrophoneVolume`. I could not find the setting in `webrtc_voice_engine.cc`. It is worth a test on a headset, since that would change the headset's own volume.

## What to check in the slim-m Windows CI build

- The plugin builds with the new `flutter_rate_converter.cc` (added to `windows/CMakeLists.txt`). It uses `std::gcd` from `<numeric>` and no platform-specific math constants.
- `flutter_noise_suppressor.cc` calls `rnnoise_destroy`, which is exported by the vendored rnnoise build (`RNNOISE_EXPORT`, hidden visibility is fine since it links statically).
- A Windows call with Strong enabled on a 16 kHz headset microphone: `getInputGate` should report `strongNoiseSuppression: true`.
