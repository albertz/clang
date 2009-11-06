//== ReturnPointerRangeChecker.cpp ------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ReturnPointerRangeChecker, which is a path-sensitive check
// which looks for an out-of-bound pointer being returned to callers.
//
//===----------------------------------------------------------------------===//

#include "GRExprEngineInternalChecks.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/Analysis/PathSensitive/CheckerVisitor.h"

using namespace clang;

namespace {
class VISIBILITY_HIDDEN ReturnPointerRangeChecker : 
    public CheckerVisitor<ReturnPointerRangeChecker> {      
  BuiltinBug *BT;
public:
    ReturnPointerRangeChecker() : BT(0) {}
    static void *getTag();
    void PreVisitReturnStmt(CheckerContext &C, const ReturnStmt *RS);
};
}

void clang::RegisterReturnPointerRangeChecker(GRExprEngine &Eng) {
  Eng.registerCheck(new ReturnPointerRangeChecker());
}

void *ReturnPointerRangeChecker::getTag() {
  static int x = 0; return &x;
}

void ReturnPointerRangeChecker::PreVisitReturnStmt(CheckerContext &C,
                                                   const ReturnStmt *RS) {
  const GRState *state = C.getState();

  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;
 
  SVal V = state->getSVal(RetE);
  const MemRegion *R = V.getAsRegion();

  const ElementRegion *ER = dyn_cast_or_null<ElementRegion>(R);
  if (!ER)
    return;  

  DefinedOrUnknownSVal &Idx = cast<DefinedOrUnknownSVal>(ER->getIndex());

  // Zero index is always in bound, this also passes ElementRegions created for
  // pointer casts.
  if (Idx.isZeroConstant())
    return;

  SVal NumVal = C.getStoreManager().getSizeInElements(state,
                                                      ER->getSuperRegion());
  DefinedOrUnknownSVal &NumElements = cast<DefinedOrUnknownSVal>(NumVal);

  const GRState *StInBound = state->AssumeInBound(Idx, NumElements, true);
  const GRState *StOutBound = state->AssumeInBound(Idx, NumElements, false);
  if (StOutBound && !StInBound) {
    ExplodedNode *N = C.GenerateNode(RS, StOutBound, true);

    if (!N)
      return;
  
    if (!BT)
      BT = new BuiltinBug("Return of Pointer Value Outside of Expected Range");
  
    // Generate a report for this bug.
    RangedBugReport *report = 
      new RangedBugReport(*BT, BT->getDescription().c_str(), N);

    report->addRange(RS->getSourceRange());
  
    C.EmitReport(report);
  }
}