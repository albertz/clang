// RUN: %clang_cc1 -fms-extensions -rewrite-objc -x objective-c++ -fblocks -o - %s
// radar 7540194

extern "C" __declspec(dllexport) void BreakTheRewriter(int i) {
        __block int aBlockVariable = 0;
        void (^aBlock)(void) = ^ {
                aBlockVariable = 42;
        };
        aBlockVariable++;
	if (i) {
	  __block int bbBlockVariable = 0;
	  void (^aBlock)(void) = ^ {
                bbBlockVariable = 42;
          };
        }
}

__declspec(dllexport) extern "C" __declspec(dllexport) void XXXXBreakTheRewriter(void) {

        __block int aBlockVariable = 0;
        void (^aBlock)(void) = ^ {
                aBlockVariable = 42;
        };
        aBlockVariable++;
        void (^bBlocks)(void) = ^ {
                aBlockVariable = 43;
        };
        void (^c)(void) = ^ {
                aBlockVariable = 44;
        };

}

@interface I
{
   id list;
}
- (void) Meth;
// radar 7589385 use before definition
- (void) allObjects;
@end

@implementation I
// radar 7589385 use before definition
- (void) allObjects {
    __attribute__((__blocks__(byref))) id *listp;

    ^(void) {
      *listp++ = 0;
    };
}
- (void) Meth { __attribute__((__blocks__(byref))) void ** listp = (void **)list; }
@end

// $CLANG -cc1 -fms-extensions -rewrite-objc -x objective-c++ -fblocks bug.mm
// g++ -c -D"__declspec(X)=" bug.cpp
