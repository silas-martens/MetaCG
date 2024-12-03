// RUN: %metacc clang++ %s -emit-llvm -S -o - | %filecheck %s
#include <stdio.h>

class A {
 public:
  virtual int foo() {
    printf("A");
    return 1;
  }
};

class B : public A {
 public:
  int foo() {
    printf("B");
    return 2;
  }
};

void caller() {
  A a;

  B* b_pointer = new B();
  A* a_pointer = new A();

  a_pointer->foo();
  b_pointer->foo();
  a.foo();

  delete a_pointer;
  delete b_pointer;
}

// CHECK: define dso_local void @_Z6callerv(
// CHECK: call void @__metacg_indirect_call
// CHECK: call noundef i32 %{{[0-9]+}}(

// CHECK: call void @__metacg_indirect_call
// CHECK: call noundef i32 %{{[0-9]+}}(
// CHECK: call noundef i32 @_ZN1A3fooEv(

// CHECK: declare void @__metacg_indirect_call(
