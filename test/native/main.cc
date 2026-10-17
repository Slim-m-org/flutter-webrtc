#include "check.h"

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

int& Failures() {
  static int failures = 0;
  return failures;
}

int main() {
  for (const TestCase& test : Registry()) {
    const int before = Failures();
    test.body();
    std::printf("%s %s\n", Failures() == before ? "ok  " : "FAIL", test.name);
  }
  std::printf("%d failure(s)\n", Failures());
  return Failures() == 0 ? 0 : 1;
}
