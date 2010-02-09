// RUN: %clang_cc1 -fsyntax-only -faccess-control -verify %s

struct Base {
  int data;
  int method();
};
int (Base::*data_ptr) = &Base::data;
int (Base::*method_ptr)() = &Base::method;

namespace test0 {
  struct Derived : Base {};
  void test() {
    int (Derived::*d) = data_ptr;
    int (Derived::*m)() = method_ptr;
  }
}

// FIXME: can't be inaccessible.
namespace test1 {
  struct Derived : private Base {};
  void test() {
    int (Derived::*d) = data_ptr; // error
    int (Derived::*m)() = method_ptr; // error
  }
};

// Can't be ambiguous.
namespace test2 {
  struct A : Base {};
  struct B : Base {};
  struct Derived : A, B {};
  void test() {
    int (Derived::*d) = data_ptr; // expected-error {{ambiguous conversion from pointer to member of base class 'struct Base' to pointer to member of derived class 'struct test2::Derived'}}
    int (Derived::*m)() = method_ptr; // expected-error {{ambiguous conversion from pointer to member of base class 'struct Base' to pointer to member of derived class 'struct test2::Derived'}}
  }
}

// Can't be virtual.
namespace test3 {
  struct Derived : virtual Base {};
  void test() {
    int (Derived::*d) = data_ptr;  // expected-error {{conversion from pointer to member of class 'struct Base' to pointer to member of class 'struct test3::Derived' via virtual base 'struct Base' is not allowed}}
    int (Derived::*m)() = method_ptr; // expected-error {{conversion from pointer to member of class 'struct Base' to pointer to member of class 'struct test3::Derived' via virtual base 'struct Base' is not allowed}}
  }
}

// Can't be virtual even if there's a non-virtual path.
namespace test4 {
  struct A : Base {};
  struct Derived : Base, virtual A {};
  void test() {
    int (Derived::*d) = data_ptr; // expected-error {{ambiguous conversion from pointer to member of base class 'struct Base' to pointer to member of derived class 'struct test4::Derived'}}
    int (Derived::*m)() = method_ptr; // expected-error {{ambiguous conversion from pointer to member of base class 'struct Base' to pointer to member of derived class 'struct test4::Derived'}}
  }
}

// PR6254: don't get thrown off by a virtual base.
namespace test5 {
  struct A {};
  struct Derived : Base, virtual A {};
  void test() {
    int (Derived::*d) = data_ptr;
    int (Derived::*m)() = method_ptr;
  }
}