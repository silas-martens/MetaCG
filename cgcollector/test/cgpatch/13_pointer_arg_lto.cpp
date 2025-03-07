// RUN: %metacc --points-to-analysis --patch-file /dev/null --verbose clang++ %s -o %s.o | %filecheck %s

template <typename F>
void foo(F& f) {
  f();
}

template <typename F>
void foo_with_arg(F& f, int x) {
  f(x);
}

void bar() {}

void bar_with_arg(int x) {}

void caller(int x) {
  // CHECK: Indirect call:
  // CHECK: may target: _Z3barv
  foo(bar);
  // CHECK: Indirect call:
  // CHECK: may target: _Z12bar_with_argi
  foo_with_arg(bar_with_arg, x);
}

int main(int argc, char** argv) {
  caller(argc);
  return 0;
}

