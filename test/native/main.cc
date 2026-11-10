#include "check.h"

int main(int argc, char** argv) {
  ParseArgs(argc, argv);
  RunRegistry();
  return Finish();
}
