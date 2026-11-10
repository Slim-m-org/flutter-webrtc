#include "check.h"

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

int& Failures() {
  static int failures = 0;
  return failures;
}
