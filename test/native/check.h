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

struct Registrar {
  Registrar(const char* name, std::function<void()> body) {
    Registry().push_back({name, std::move(body)});
  }
};

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

#endif  // TEST_NATIVE_CHECK_H_
