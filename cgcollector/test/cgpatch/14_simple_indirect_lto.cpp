// RUN: %metacc --points-to-analysis --patch-file /dev/null --verbose clang++ %s -o %s.o | %filecheck %s

typedef void (*FuncPtr)();
void foo() { }
void bar() { }

void callIndirect(FuncPtr fptr) {
  fptr();  // Indirect call
  // CHECK: Indirect call:
  // CHECK: may target: _Z3foov
}

int main() {
  FuncPtr f = foo;
  callIndirect(f);
  return 1;
}


