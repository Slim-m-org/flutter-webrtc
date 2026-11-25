import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_webrtc/src/native/desktop_capturer_impl.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('a loopback error reaches onLoopbackError with the source id', () {
    final capturer = DesktopCapturerNative.instance;
    final ids = <String>[];
    final captureErrors = <String>[];
    final loopback = capturer.onLoopbackError.stream.listen(ids.add);
    final capture = capturer.onCaptureError.stream.listen(captureErrors.add);

    capturer.handleEvent('loopbackError', {'id': 'screen:0:0'});

    expect(ids, ['screen:0:0']);
    expect(captureErrors, isEmpty);
    loopback.cancel();
    capture.cancel();
  });
}
