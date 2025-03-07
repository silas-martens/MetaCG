// RUN: %metacc --points-to-analysis --patch-file /dev/null  --verbose clang++ %s -o %s.o | %filecheck %s


typedef void (*FuncPtr)();
void foo() { }
void bar() { }

void callIndirect(FuncPtr* fptrs, int n) {
  fptrs[n]();  // Indirect call
  // CHECK: Indirect call:
  // CHECK: may target: _Z3foov, _Z3barv
}

int main(int argc, char** argv) {
  FuncPtr fptrs[1];
  fptrs[0] = foo;
  fptrs[1] = bar;
  callIndirect(fptrs, argc);
  return 1;
}


