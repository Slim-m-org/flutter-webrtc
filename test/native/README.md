# Native tests

Standalone C++ tests for the capture chain stages (strong noise suppression,
the rate converter, the input gate with its lookahead, the burst checks over the whole chain) and the screen share size limit.
They also cover the small thread-safety helpers: the loopback session, the serial executor, the locked map and the request coalescer.
They stub the libwebrtc headers, so nothing is downloaded.

```sh
cmake -S test/native -B build/native-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-tests -j4
build/native-tests/native_tests
build/native-tests/thread_tests
```

The binary exits non-zero if any case fails and prints the measured numbers
(noise attenuation, resampler gain and delay, allocation counts) per rate.

`thread_tests` holds the thread-safety helpers' cases, apart from the rest so it can be built with a race detector.
A missing lock only shows under one, so also configure a second build directory with `-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-fsanitize=thread` and build `--target thread_tests`.

`portal_guard_tests` runs the Linux portal session guard on a private D-Bus session bus.
It is built when glib is found, and prints `skip` when there is no `dbus-daemon`.

`strongest_tool in.f32 out.f32 strongest` runs a raw 48 kHz float file through
the suppressor and prints the real-time factor; its tuning comes from the
environment (see the top of `strongest_tool.cc`). Both targets build the
DeepFilterNet library with cargo, so they need a Rust toolchain; without one
the build warns and Strongest falls back to Strong.
