//===--- TypeChecker.cpp - Type Checking ----------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for expressions, and other pieces
// that require final type checking.  If this passes a translation unit with no
// errors, then it is good to go.
//
//===----------------------------------------------------------------------===//

#include "TypeChecker.h"
#include "swift/AST/ASTVisitor.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/SourceMgr.h"
using namespace swift;


//===----------------------------------------------------------------------===//
// BindAndValidateClosureArgs - When a closure is formed, this walks an AST to
// update AnonClosureArgExpr to be of the right type.
//===----------------------------------------------------------------------===//

namespace {
struct RewriteAnonArgExpr : Walker {
  Type FuncInputTy;
  TypeChecker &TC;
  
  RewriteAnonArgExpr(Type funcInputTy, TypeChecker &tc)
    : FuncInputTy(funcInputTy), TC(tc) {}
  
  bool walkToExprPre(Expr *E) {
    // If this is a ClosureExpr, don't walk into it.  This would find *its*
    // anonymous closure arguments, not ours.
    // FIXME: This should only stop at *explicit* closures.
    if (isa<ClosureExpr>(E)) return false; // Don't recurse into it.
      
    // Otherwise, do recurse into it.  We handle anon args in the postorder
    // visitation.
    return true;
  }

  Expr *walkToExprPost(Expr *E) {
    // If we found a closure argument, process it.
    AnonClosureArgExpr *A = dyn_cast<AnonClosureArgExpr>(E);
    if (A == 0) return E;  
    
    // If the input to the function is a non-tuple, only $0 is valid, if it is a
    // tuple, then $0..$N are valid depending on the number of inputs to the
    // tuple.
    unsigned NumInputArgs = 1;
    if (TupleType *TT = dyn_cast<TupleType>(FuncInputTy.getPointer()))
      NumInputArgs = TT->getFields().size();
    
    assert(A->getType()->is<DependentType>() && "Anon arg already has a type?");
    
    // Verify that the argument number isn't too large, e.g. using $4 when the
    // bound function only has 2 inputs.
    if (A->getArgNumber() >= NumInputArgs) {
      TC.diagnose(A->getLoc(), diag::invalid_anonymous_argument,
                  A->getArgNumber(), NumInputArgs);
      return 0;
    }
    
    // Assign the AnonDecls their actual concrete types now that we know the
    // context they are being used in.
    if (TupleType *TT = dyn_cast<TupleType>(FuncInputTy.getPointer())) {
      A->setType(LValueType::get(TT->getElementType(A->getArgNumber()),
                                 LValueType::Qual::Default, TC.Context));
    } else {
      assert(NumInputArgs == 1 && "Must have unary case");
      A->setType(LValueType::get(FuncInputTy, LValueType::Qual::Default,
                                 TC.Context));
    }
    return A;
  }
};
} // end anonymous namespace

/// bindAndValidateClosureArgs - The specified list of anonymous closure
/// arguments was bound to a closure function with the specified input
/// arguments.  Validate the argument list and, if valid, allocate and return
/// a pointer to the argument to be used for the ClosureExpr.
bool TypeChecker::bindAndValidateClosureArgs(Expr *Body, Type FuncInput) {  
  RewriteAnonArgExpr Rewriter(FuncInput, *this);
  
  // Walk the body and rewrite any anonymous arguments.  Note that this
  // isn't a particularly efficient way to handle this, because we walk subtrees
  // even if they have no anonymous arguments.
  return Body->walk(Rewriter) == nullptr;
}



