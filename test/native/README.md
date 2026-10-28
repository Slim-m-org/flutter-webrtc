# Native tests

Standalone C++ tests for the capture chain stages (strong noise suppression,
the rate converter, the input gate with its lookahead, the burst checks over the whole chain). They stub the libwebrtc header, so
nothing is downloaded.

```sh
cmake -S test/native -B build/native-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-tests -j4
build/native-tests/native_tests
```

The binary exits non-zero if any case fails and prints the measured numbers
(noise attenuation, resampler gain and delay, allocation counts) per rate.

`strongest_tool in.f32 out.f32 strongest` runs a raw 48 kHz float file through
the suppressor and prints the real-time factor; its tuning comes from the
environment (see the top of `strongest_tool.cc`). Both targets build the
DeepFilterNet library with cargo, so they need a Rust toolchain; without one
the build warns and Strongest falls back to Strong.
