#include "check.h"
#include "com_scope.h"

namespace {

using flutter_webrtc_plugin::ComScope;

struct Counting {
  static bool Enter() {
    ++enters;
    return joins;
  }
  static void Leave() { ++leaves; }
  static void Reset(bool join) {
    enters = leaves = 0;
    joins = join;
  }
  static int enters;
  static int leaves;
  static bool joins;
};
int Counting::enters = 0;
int Counting::leaves = 0;
bool Counting::joins = true;

TEST(ComScope_LeavesTheApartmentItJoined) {
  Counting::Reset(true);
  {
    ComScope<Counting> scope;
    CHECK(scope.joined());
    CHECK(Counting::enters == 1);
    CHECK(Counting::leaves == 0);
  }
  CHECK(Counting::leaves == 1);
}

TEST(ComScope_DoesNotLeaveAnApartmentItNeverJoined) {
  Counting::Reset(false);
  {
    ComScope<Counting> scope;
    CHECK(!scope.joined());
  }
  CHECK(Counting::leaves == 0);
}

}  // namespace
