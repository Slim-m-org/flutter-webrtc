#ifndef TEST_NATIVE_CHECK_H_
#define TEST_NATIVE_CHECK_H_

#include <cstdio>
#include <functional>
#include <vector>

struct TestCase {
  const char* name;
  std::function<void()> body;
};

std::vector<TestCase>& Registry();
int& Failures();
int& Skips();

struct Registrar {
  Registrar(const char* name, std::function<void()> body) {
    Registry().push_back({name, std::move(body)});
  }
};

// Reads --strict (a skip fails the run) and --shuffle <seed> (a random order).
void ParseArgs(int argc, char** argv);

// Runs before each test, so a binary with shared state puts it back there.
void SetBeforeEach(void (*hook)());

// Runs every registered test and prints one line for each.
void RunRegistry();

// Prints the totals and returns the process exit code.
int Finish();

// For a binary that cannot start at all; strict turns it into a failure.
int SuiteSkipped(const char* reason);

#define TEST(name)                                       \
  static void name();                                    \
  static Registrar registrar_##name(#name, name);        \
  static void name()

#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) {                                                   \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
      ++Failures();                                                  \
    }                                                                \
  } while (0)

// Ends a test that cannot run here; only in the TEST body, where return leaves it.
#define SKIP(reason)                                                 \
  do {                                                               \
    std::printf("  SKIP %s:%d: %s\n", __FILE__, __LINE__, reason);   \
    ++Skips();                                                       \
    return;                                                          \
  } while (0)

#endif  // TEST_NATIVE_CHECK_H_
