// RUN: %clang_cc1 -emit-llvm -o %t %s
template <typename T>
class A
{
    union { void *d; };

    A() : d(0) { }
};

A<int> a0;
