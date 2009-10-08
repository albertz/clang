// Note: the run lines follow their respective tests, since line/column
// matter in this test.

@interface Foo  {
  void *isa;
}
@property(copy) Foo *myprop;
@property(retain, nonatomic) id xx;
// RUN: clang-cc -fsyntax-only -code-completion-at=%s:7:11 %s -o - | FileCheck -check-prefix=CC1 %s &&
// CC1: readonly
// CC1-NEXT: assign
// CC1-NEXT: readwrite
// CC1-NEXT: retain
// CC1-NEXT: copy
// CC1-NEXT: nonatomic
// CC1-NEXT: setter
// CC1-NEXT: getter
// RUN: clang-cc -fsyntax-only -code-completion-at=%s:8:18 %s -o - | FileCheck -check-prefix=CC2 %s
// CC2: readonly
// CC2-NEXT: assign
// CC2-NEXT: readwrite
// CC2-NEXT: copy
// CC2-NEXT: nonatomic
// CC2-NEXT: setter
// CC2-NEXT: getter
@end


