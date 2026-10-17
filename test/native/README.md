# Native tests

Standalone C++ tests for the capture chain stages (strong noise suppression,
the rate converter, the input gate). They stub the libwebrtc header, so
nothing is downloaded.

```sh
cmake -S test/native -B build/native-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-tests -j4
build/native-tests/native_tests
```

The binary exits non-zero if any case fails and prints the measured numbers
(noise attenuation, resampler gain and delay, allocation counts) per rate.
