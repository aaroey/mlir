//===- Value.cpp - MLIR Value Classes -------------------------------------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#include "mlir/IR/Value.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Operation.h"
using namespace mlir;

/// If this value is the result of an Operation, return the operation that
/// defines it.
Operation *Value::getDefiningOp() {
  if (auto *result = dyn_cast<OpResult>(this))
    return result->getOwner();
  return nullptr;
}

/// Return the function that this Value is defined in.
Function *Value::getFunction() {
  switch (getKind()) {
  case Value::Kind::BlockArgument:
    return cast<BlockArgument>(this)->getFunction();
  case Value::Kind::OpResult:
    return getDefiningOp()->getFunction();
  }
}

Location Value::getLoc() {
  if (auto *op = getDefiningOp()) {
    return op->getLoc();
  }
  return UnknownLoc::get(getContext());
}

//===----------------------------------------------------------------------===//
// IRObjectWithUseList implementation.
//===----------------------------------------------------------------------===//

/// Replace all uses of 'this' value with the new value, updating anything in
/// the IR that uses 'this' to use the other value instead.  When this returns
/// there are zero uses of 'this'.
void IRObjectWithUseList::replaceAllUsesWith(IRObjectWithUseList *newValue) {
  assert(this != newValue && "cannot RAUW a value with itself");
  while (!use_empty()) {
    use_begin()->set(newValue);
  }
}

/// Drop all uses of this object from their respective owners.
void IRObjectWithUseList::dropAllUses() {
  while (!use_empty()) {
    use_begin()->drop();
  }
}

//===----------------------------------------------------------------------===//
// BlockArgument implementation.
//===----------------------------------------------------------------------===//

/// Return the function that this argument is defined in.
Function *BlockArgument::getFunction() {
  if (auto *owner = getOwner())
    return owner->getFunction();
  return nullptr;
}

/// Returns if the current argument is a function argument.
bool BlockArgument::isFunctionArgument() {
  auto *containingFn = getFunction();
  return containingFn && &containingFn->front() == getOwner();
}
