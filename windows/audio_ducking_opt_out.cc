#include "audio_ducking_opt_out.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdio>
#include <mutex>
#include <vector>

namespace flutter_webrtc_plugin {

namespace {

using Microsoft::WRL::ComPtr;

void LogFailure(const char* step, HRESULT hr) {
  char line[128];
  std::snprintf(line, sizeof(line), "audio ducking opt-out: %s failed 0x%08lx\n",
                step, static_cast<unsigned long>(hr));
  OutputDebugStringA(line);
}

// The default session only lives while something references it, and the
// prebuilt module opens its stream later. Holding the controls keeps the
// preference on the session that stream joins. Leaked on purpose so process
// teardown never releases them after COM is gone.
std::vector<ComPtr<IAudioSessionControl2>>& HeldControls() {
  static auto* held = new std::vector<ComPtr<IAudioSessionControl2>>();
  return *held;
}

std::mutex& HeldMutex() {
  static auto* mutex = new std::mutex();
  return *mutex;
}

bool OptOutEndpoint(IMMDevice* device,
                    std::vector<ComPtr<IAudioSessionControl2>>* out) {
  ComPtr<IAudioSessionManager2> manager;
  HRESULT hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                                nullptr, reinterpret_cast<void**>(
                                             manager.GetAddressOf()));
  if (FAILED(hr)) {
    LogFailure("Activate(IAudioSessionManager2)", hr);
    return false;
  }
  // A null session GUID is the process default session, which is the one the
  // module's streams are created in.
  ComPtr<IAudioSessionControl> control;
  hr = manager->GetAudioSessionControl(nullptr, 0, control.GetAddressOf());
  if (FAILED(hr)) {
    LogFailure("GetAudioSessionControl", hr);
    return false;
  }
  ComPtr<IAudioSessionControl2> control2;
  hr = control.As(&control2);
  if (FAILED(hr)) {
    LogFailure("QueryInterface(IAudioSessionControl2)", hr);
    return false;
  }
  hr = control2->SetDuckingPreference(TRUE);
  if (FAILED(hr)) {
    LogFailure("SetDuckingPreference", hr);
    return false;
  }
  out->push_back(control2);
  return true;
}

void OptOutDataFlow(IMMDeviceEnumerator* enumerator, EDataFlow flow,
                    std::vector<ComPtr<IAudioSessionControl2>>* out) {
  ComPtr<IMMDeviceCollection> devices;
  HRESULT hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE,
                                              devices.GetAddressOf());
  if (FAILED(hr)) {
    LogFailure("EnumAudioEndpoints", hr);
    return;
  }
  UINT count = 0;
  hr = devices->GetCount(&count);
  if (FAILED(hr)) {
    LogFailure("GetCount", hr);
    return;
  }
  for (UINT i = 0; i < count; i++) {
    ComPtr<IMMDevice> device;
    hr = devices->Item(i, device.GetAddressOf());
    if (FAILED(hr)) {
      LogFailure("Item", hr);
      continue;
    }
    OptOutEndpoint(device.Get(), out);
  }
}

}  // namespace

void OptOutOfAudioDucking() {
  // Multithreaded so the held controls stay usable from any thread. The
  // apartment is never torn down for the same reason: a caller that is
  // already single threaded gets RPC_E_CHANGED_MODE and works in its own.
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
    LogFailure("CoInitializeEx", init);
    return;
  }
  ComPtr<IMMDeviceEnumerator> enumerator;
  const HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
      __uuidof(IMMDeviceEnumerator),
      reinterpret_cast<void**>(enumerator.GetAddressOf()));
  if (FAILED(hr)) {
    LogFailure("CoCreateInstance(MMDeviceEnumerator)", hr);
    return;
  }
  std::vector<ComPtr<IAudioSessionControl2>> fresh;
  OptOutDataFlow(enumerator.Get(), eRender, &fresh);
  OptOutDataFlow(enumerator.Get(), eCapture, &fresh);
  std::lock_guard<std::mutex> lock(HeldMutex());
  HeldControls().swap(fresh);
}

}  // namespace flutter_webrtc_plugin
