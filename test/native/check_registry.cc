#include "check.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>

namespace {

bool g_strict = false;
bool g_shuffle = false;
unsigned g_seed = 0;
void (*g_before_each)() = nullptr;

}  // namespace

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

int& Failures() {
  static int failures = 0;
  return failures;
}

int& Skips() {
  static int skips = 0;
  return skips;
}

void ParseArgs(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--strict") == 0) {
      g_strict = true;
    } else if (std::strcmp(argv[i], "--shuffle") == 0 && i + 1 < argc) {
      g_shuffle = true;
      g_seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
    } else {
      std::printf("unknown argument: %s\n", argv[i]);
      std::exit(2);
    }
  }
}

void SetBeforeEach(void (*hook)()) { g_before_each = hook; }

void RunRegistry() {
  std::vector<const TestCase*> order;
  for (const TestCase& test : Registry()) order.push_back(&test);
  if (g_shuffle) {
    std::shuffle(order.begin(), order.end(), std::mt19937(g_seed));
    std::printf("shuffled with seed %u\n", g_seed);
  }
  for (const TestCase* test : order) {
    const int failed_before = Failures();
    const int skipped_before = Skips();
    if (g_before_each != nullptr) g_before_each();
    test->body();
    const char* outcome = Failures() != failed_before   ? "FAIL"
                          : Skips() != skipped_before ? "skip"
                                                       : "ok  ";
    std::printf("%s %s\n", outcome, test->name);
  }
}

int Finish() {
  std::printf("%d failure(s), %d skipped\n", Failures(), Skips());
  if (g_strict && Skips() > 0) {
    std::printf("strict: a skipped test counts as a failure\n");
    return 1;
  }
  return Failures() == 0 ? 0 : 1;
}

int SuiteSkipped(const char* reason) {
  std::printf("skip: %s\n", reason);
  return g_strict ? 1 : 0;
}
