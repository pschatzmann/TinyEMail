// Desktop unit tests for TinyVector.

#include <iostream>

#include <Arduino.h>

#include "TinyEMail/TinyVector.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond  \
                << "\n";                                                     \
    }                                                                        \
  } while (0)

// counts the live objects, to catch leaks and double destruction
struct Counted {
  static int live;
  int value = 0;
  Counted() { live++; }
  explicit Counted(int v) : value(v) { live++; }
  Counted(const Counted& other) : value(other.value) { live++; }
  Counted& operator=(const Counted& other) { value = other.value; return *this; }
  ~Counted() { live--; }
};
int Counted::live = 0;

static void testPushBackGrows() {
  TinyVector<int> v;
  CHECK(v.empty());
  for (int i = 0; i < 100; i++) {
    CHECK(v.push_back(i));
  }
  CHECK(v.size() == 100);
  CHECK(v.capacity() >= 100);
  CHECK(v[0] == 0 && v[99] == 99);
  int sum = 0;
  for (int x : v) sum += x;
  CHECK(sum == 4950);
}

static void testCountConstructor() {
  TinyVector<int> v(5);
  CHECK(v.size() == 5);
  CHECK(v[4] == 0);
}

static void testCopyIsIndependent() {
  TinyVector<Counted> a;
  a.push_back(Counted(1));
  a.push_back(Counted(2));
  TinyVector<Counted> b = a;
  CHECK(b.size() == 2 && b[1].value == 2);
  b[0].value = 42;
  CHECK(a[0].value == 1);  // the copy does not share memory
  a = b;
  CHECK(a[0].value == 42);
}

static void testClearAndDestructors() {
  CHECK(Counted::live == 0);
  {
    TinyVector<Counted> v;
    for (int i = 0; i < 10; i++) v.push_back(Counted(i));
    CHECK(Counted::live == 10);
    v.clear();
    CHECK(Counted::live == 0);
    v.push_back(Counted(7));
    TinyVector<Counted> copy(v);
    CHECK(Counted::live == 2);
  }
  CHECK(Counted::live == 0);
}

static void testAssignmentFreesOldElements() {
  TinyVector<Counted> a(3);
  CHECK(Counted::live == 3);
  TinyVector<Counted> b(2);
  a = b;
  CHECK(Counted::live == 4);  // the 3 old elements are gone: 2 copies in a, 2 in b
}

static void testReserveKeepsElements() {
  TinyVector<int> v;
  v.push_back(5);
  CHECK(v.reserve(50));
  CHECK(v.capacity() >= 50);
  CHECK(v.size() == 1 && v[0] == 5);
}

static void testDesktopUsesHeap() {
  TinyVector<int> v;
  v.push_back(1);
  CHECK(!v.inPsram());  // no PSRAM on the desktop
}

int main() {
  testPushBackGrows();
  testCountConstructor();
  testCopyIsIndependent();
  testClearAndDestructors();
  testAssignmentFreesOldElements();
  testReserveKeepsElements();
  testDesktopUsesHeap();

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
