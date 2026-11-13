# Native tests

Standalone C++ tests for the capture chain stages (strong noise suppression,
the rate converter, the input gate with its lookahead, the burst checks over the whole chain) the screen share size limit, and the render limiter that carries a per-person volume above 100% without clipping.
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

`ctest --test-dir build/native-tests` runs all of them, plus the checks of the harness and of the libwebrtc download pin.
Configure with `-DNATIVE_TESTS_STRICT=ON`, as CI does, and a skipped test fails the run instead of being reported as `skip`.
A test skips with `SKIP("reason")` when it cannot run here, such as the Strongest test in a build without cargo.
`native_tests --shuffle <seed>` runs the cases in another order, to find one that leans on state an earlier case left behind.
Every case starts from the same state of the capture chain singletons, set in `shared_state_test.cc`.

`libwebrtc_pin_test.cmake` runs `third_party/CMakeLists.txt` against a local release folder.
It checks that the prebuilt libwebrtc is only used when it hashes to the `sha256_<os>_<arch>` pinned in `third_party/libwebrtc_version.ini`.

`.github/workflows/native-tests.yml` runs this suite on Linux, and on Windows without failing the workflow until it has run green there.

`thread_tests` holds the thread-safety helpers' cases, apart from the rest so it can be built with a race detector.
A missing lock only shows under one, so also configure a second build directory with `-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-fsanitize=thread` and build `--target thread_tests`.

On Linux `native_tests` also wraps the DeepFilterNet create and free calls at link time (`strongest_model_threads_test.cc`), to check that neither the platform thread nor the audio thread loads or frees the model.
The Windows linker has no `--wrap`, so those cases are Linux only.

`portal_guard_tests` runs the Linux portal session guard on a private D-Bus session bus.
It is built when glib is found, and prints `skip` when there is no `dbus-daemon`.

`strongest_tool in.f32 out.f32 strongest` runs a raw 48 kHz float file through
the suppressor and prints the real-time factor; its tuning comes from the
environment (see the top of `strongest_tool.cc`). Both targets build the
DeepFilterNet library with cargo, so they need a Rust toolchain; without one
the build warns and Strongest falls back to Strong.
