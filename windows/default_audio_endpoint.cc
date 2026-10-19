#include "default_audio_endpoint.h"

#include <windows.h>
#include <mmdeviceapi.h>

namespace flutter_webrtc_plugin {

namespace {

std::string ToUtf8(const wchar_t* wide) {
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return std::string();
  std::string out(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr,
                      nullptr);
  return out;
}

}  // namespace

std::string DefaultCallEndpointId(bool capture) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool must_uninit = SUCCEEDED(init);
  std::string id;
  IMMDeviceEnumerator* enumerator = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&enumerator)))) {
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
            capture ? eCapture : eRender, capture ? eCommunications : eConsole,
            &device))) {
      LPWSTR wide_id = nullptr;
      if (SUCCEEDED(device->GetId(&wide_id)) && wide_id != nullptr) {
        id = ToUtf8(wide_id);
        CoTaskMemFree(wide_id);
      }
      device->Release();
    }
    enumerator->Release();
  }
  if (must_uninit) CoUninitialize();
  return id;
}

}  // namespace flutter_webrtc_plugin
