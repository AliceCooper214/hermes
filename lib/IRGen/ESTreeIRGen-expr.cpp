/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "ESTreeIRGen.h"

namespace hermes {
namespace irgen {

Value *ESTreeIRGen::genExpression(ESTree::Node *expr, Identifier nameHint) {
  DEBUG(dbgs() << "IRGen expression of type " << expr->getNodeName() << "\n");
  IRBuilder::ScopedLocationChange slc(Builder, expr->getDebugLoc());

  // Handle identifiers.
  if (auto *Iden = dyn_cast<ESTree::IdentifierNode>(expr)) {
    return genIdentifierExpression(Iden, false);
  }

  // Handle Null Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-null-literals
  if (isa<ESTree::NullLiteralNode>(expr)) {
    return Builder.getLiteralNull();
  }

  // Handle String Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-literals-string-literals
  if (auto *Lit = dyn_cast<ESTree::StringLiteralNode>(expr)) {
    DEBUG(dbgs() << "Loading String Literal \"" << Lit->_value << "\"\n");
    return Builder.getLiteralString(Lit->_value->str());
  }

  // Handle Regexp Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-literals-regular-expression-literals
  if (auto *Lit = dyn_cast<ESTree::RegExpLiteralNode>(expr)) {
    DEBUG(
        dbgs() << "Loading regexp Literal \"" << Lit->_pattern->str() << " / "
               << Lit->_flags->str() << "\"\n");

    return Builder.createRegExpInst(
        Identifier::getFromPointer(Lit->_pattern),
        Identifier::getFromPointer(Lit->_flags));
  }

  // Handle Boolean Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-boolean-literals
  if (auto *Lit = dyn_cast<ESTree::BooleanLiteralNode>(expr)) {
    DEBUG(dbgs() << "Loading String Literal \"" << Lit->_value << "\"\n");
    return Builder.getLiteralBool(Lit->_value);
  }

  // Handle Number Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-literals-numeric-literals
  if (auto *Lit = dyn_cast<ESTree::NumericLiteralNode>(expr)) {
    DEBUG(dbgs() << "Loading Numeric Literal \"" << Lit->_value << "\"\n");
    return Builder.getLiteralNumber(Lit->_value);
  }

  // Handle the assignment expression.
  if (auto Assign = dyn_cast<ESTree::AssignmentExpressionNode>(expr)) {
    return genAssignmentExpr(Assign);
  }

  // Handle Call expressions.
  if (auto *call = dyn_cast<ESTree::CallExpressionNode>(expr)) {
    return genCallExpr(call);
  }

  // Handle the 'new' expressions.
  if (auto *newExp = dyn_cast<ESTree::NewExpressionNode>(expr)) {
    return genNewExpr(newExp);
  }

  // Handle MemberExpression expressions for access property.
  if (auto *Mem = dyn_cast<ESTree::MemberExpressionNode>(expr)) {
    LReference lref = createLRef(Mem);
    return lref.emitLoad(Builder);
  }

  // Handle Array expressions (syntax: [1,2,3]).
  if (auto *Arr = dyn_cast<ESTree::ArrayExpressionNode>(expr)) {
    return genArrayExpr(Arr);
  }

  // Handle object expressions (syntax: {"1" : "2"}).
  if (auto *Obj = dyn_cast<ESTree::ObjectExpressionNode>(expr)) {
    return genObjectExpr(Obj);
  }

  // Handle logical expressions (short circuiting).
  if (auto *L = dyn_cast<ESTree::LogicalExpressionNode>(expr)) {
    return genLogicalExpression(L);
  }

  // Handle Binary Expressions.
  if (auto *Bin = dyn_cast<ESTree::BinaryExpressionNode>(expr)) {
    Value *LHS = genExpression(Bin->_left);
    Value *RHS = genExpression(Bin->_right);

    auto Kind = BinaryOperatorInst::parseOperator(Bin->_operator->str());

    return Builder.createBinaryOperatorInst(LHS, RHS, Kind);
  }

  // Handle Unary operator Expressions.
  if (auto *U = dyn_cast<ESTree::UnaryExpressionNode>(expr)) {
    return genUnaryExpression(U);
  }

  // Handle the 'this' keyword.
  if (isa<ESTree::ThisExpressionNode>(expr)) {
    if (curFunction()->function->getDefinitionKind() ==
        Function::DefinitionKind::ES6Arrow) {
      assert(
          curFunction()->capturedThis &&
          "arrow function must have a captured this");
      return Builder.createLoadFrameInst(curFunction()->capturedThis);
    }
    return curFunction()->function->getThisParameter();
  }

  if (auto *MP = dyn_cast<ESTree::MetaPropertyNode>(expr)) {
    return genMetaProperty(MP);
  }

  // Handle function expressions.
  if (auto *FE = dyn_cast<ESTree::FunctionExpressionNode>(expr)) {
    return genFunctionExpression(FE, nameHint);
  }

  if (auto *AF = dyn_cast<ESTree::ArrowFunctionExpressionNode>(expr)) {
    return genArrowFunctionExpression(AF, nameHint);
  }

  if (auto *U = dyn_cast<ESTree::UpdateExpressionNode>(expr)) {
    return genUpdateExpr(U);
  }

  if (auto *C = dyn_cast<ESTree::ConditionalExpressionNode>(expr)) {
    return genConditionalExpr(C);
  }

  if (auto *Sq = dyn_cast<ESTree::SequenceExpressionNode>(expr)) {
    return genSequenceExpr(Sq);
  }

  assert(false && "Don't know this kind of expression");
  return nullptr;
}

void ESTreeIRGen::genExpressionBranch(
    ESTree::Node *expr,
    BasicBlock *onTrue,
    BasicBlock *onFalse) {
  switch (expr->getKind()) {
    case ESTree::NodeKind::LogicalExpression:
      return genLogicalExpressionBranch(
          cast<ESTree::LogicalExpressionNode>(expr), onTrue, onFalse);

    case ESTree::NodeKind::UnaryExpression: {
      auto *e = cast<ESTree::UnaryExpressionNode>(expr);
      switch (UnaryOperatorInst::parseOperator(e->_operator->str())) {
        case UnaryOperatorInst::OpKind::BangKind:
          return genExpressionBranch(e->_argument, onFalse, onTrue);
        default:
          break;
      }

      break;
    }

    case ESTree::NodeKind::SequenceExpression: {
      auto *e = cast<ESTree::SequenceExpressionNode>(expr);

      ESTree::NodePtr last = nullptr;
      for (auto &ex : e->_expressions) {
        if (last)
          genExpression(last);
        last = &ex;
      }
      if (last)
        genExpressionBranch(last, onTrue, onFalse);
      return;
    }

    default:
      break;
  }

  Value *condVal = genExpression(expr);
  Builder.createCondBranchInst(condVal, onTrue, onFalse);
}

Value *ESTreeIRGen::genArrayExpr(ESTree::ArrayExpressionNode *Expr) {
  DEBUG(dbgs() << "Initializing a new array\n");
  AllocArrayInst::ArrayValueList elements;

  // We store consecutive elements until we encounter elision,
  // or we enounter a non-literal in limited-register mode.
  // The rest of them has to be initialized through property sets.
  unsigned count = 0;
  bool consecutive = true;
  auto codeGenOpts = Mod->getContext().getCodeGenerationSettings();
  AllocArrayInst *allocArrayInst = nullptr;
  for (auto &E : Expr->_elements) {
    Value *value{nullptr};
    if (!isa<ESTree::EmptyNode>(&E)) {
      value = genExpression(&E);
    }
    if (!value || (!isa<Literal>(value) && !codeGenOpts.unlimitedRegisters)) {
      // This is either an elision,
      // or a non-literal in limited-register mode.
      if (consecutive) {
        // So far we have been storing elements consecutively,
        // but not anymore, time to create the array.
        allocArrayInst =
            Builder.createAllocArrayInst(elements, Expr->_elements.size());
        consecutive = false;
      }
    }
    if (value) {
      if (consecutive) {
        elements.push_back(value);
      } else {
        Builder.createStoreOwnPropertyInst(value, allocArrayInst, count);
      }
    }
    count++;
  }

  if (!allocArrayInst) {
    allocArrayInst =
        Builder.createAllocArrayInst(elements, Expr->_elements.size());
  }
  if (count > 0 && isa<ESTree::EmptyNode>(&Expr->_elements.back())) {
    // Last element is an elision, VM cannot derive the length properly.
    // We have to explicitly set it.
    Builder.createStorePropertyInst(
        Builder.getLiteralNumber(count), allocArrayInst, StringRef("length"));
  }
  return allocArrayInst;
}

Value *ESTreeIRGen::genCallExpr(ESTree::CallExpressionNode *call) {
  DEBUG(dbgs() << "IRGen 'call' statement/expression.\n");

  // Check for a direct call to eval().
  if (auto *identNode = dyn_cast<ESTree::IdentifierNode>(call->_callee)) {
    if (Identifier::getFromPointer(identNode->_name) == identEval_) {
      auto *evalVar = nameTable_.lookup(identEval_);
      if (!evalVar || isa<GlobalObjectProperty>(evalVar))
        return genCallEvalExpr(call);
    }
  }

  Value *thisVal;
  Value *callee;

  // Handle MemberExpression expression calls that sets the 'this' property.
  if (auto *Mem = dyn_cast<ESTree::MemberExpressionNode>(call->_callee)) {
    Value *obj = genExpression(Mem->_object);
    Value *prop = genMemberExpressionProperty(Mem);

    // Call the callee with obj as the 'this' pointer.
    thisVal = obj;
    callee = Builder.createLoadPropertyInst(obj, prop);
  } else {
    thisVal = Builder.getLiteralUndefined();
    callee = genExpression(call->_callee);
  }

  CallInst::ArgumentList args;
  for (auto &arg : call->_arguments) {
    args.push_back(genExpression(&arg));
  }

  return Builder.createCallInst(callee, thisVal, args);
}

Value *ESTreeIRGen::genCallEvalExpr(ESTree::CallExpressionNode *call) {
  if (call->_arguments.empty()) {
    Mod->getContext().getSourceErrorManager().warning(
        call->getSourceRange(), "eval() without arguments returns undefined");
    return Builder.getLiteralUndefined();
  }

  Mod->getContext().getSourceErrorManager().warning(
      Warning::DirectEval,
      call->getSourceRange(),
      "Direct call to eval(), but lexical scope is not supported.");

  llvm::SmallVector<Value *, 1> args;
  for (auto &arg : call->_arguments) {
    args.push_back(genExpression(&arg));
  }

  if (args.size() > 1) {
    Mod->getContext().getSourceErrorManager().warning(
        call->getSourceRange(), "Extra eval() arguments are ignored");
  }

  return Builder.createDirectEvalInst(args[0]);
}

/// Convert a property key node to its JavaScript string representation.
static StringRef propertyKeyAsString(
    llvm::SmallVectorImpl<char> &storage,
    ESTree::Node *Key) {
  // Handle String Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-literals-string-literals
  if (auto *Lit = dyn_cast<ESTree::StringLiteralNode>(Key)) {
    DEBUG(dbgs() << "Loading String Literal \"" << Lit->_value << "\"\n");
    return Lit->_value->str();
  }

  // Handle identifiers as if they are String Literals.
  if (auto *Iden = dyn_cast<ESTree::IdentifierNode>(Key)) {
    DEBUG(dbgs() << "Loading String Literal \"" << Iden->_name << "\"\n");
    return Iden->_name->str();
  }

  // Handle Number Literals.
  // http://www.ecma-international.org/ecma-262/6.0/#sec-literals-numeric-literals
  if (auto *Lit = dyn_cast<ESTree::NumericLiteralNode>(Key)) {
    DEBUG(dbgs() << "Loading Numeric Literal \"" << Lit->_value << "\"\n");
    storage.resize(NUMBER_TO_STRING_BUF_SIZE);
    auto len = numberToString(Lit->_value, storage.data(), storage.size());
    return StringRef(storage.begin(), len);
  }

  llvm_unreachable("Don't know this kind of property key");
  return StringRef();
}

Value *ESTreeIRGen::genObjectExpr(ESTree::ObjectExpressionNode *Expr) {
  DEBUG(dbgs() << "Initializing a new object\n");

  /// Store information about a property. Is it an accessor (getter/setter) or
  /// a value, and the actual value.
  class PropertyValue {
   public:
    /// Is this a getter/setter value.
    bool isAccessor = false;
    /// Did we set the accessors of this property already. We need this because
    /// accessors are two separate ObjectMethod nodes, but we can only set them
    /// once.
    bool accessorsGenerated = false;
    /// The value, if this is a regular property
    ESTree::Node *valueNode{};
    /// Getter accessor, if this is an accessor property.
    ESTree::FunctionExpressionNode *getterNode{};
    /// Setter accessor, if this is an accessor property.
    ESTree::FunctionExpressionNode *setterNode{};

    SMRange getSourceRange() {
      if (valueNode) {
        return valueNode->getSourceRange();
      }

      if (getterNode) {
        return getterNode->getSourceRange();
      }

      if (setterNode) {
        return setterNode->getSourceRange();
      }

      llvm_unreachable("Unset node has no location info");
    }

    void setValue(ESTree::Node *val) {
      isAccessor = false;
      valueNode = val;
      getterNode = setterNode = nullptr;
    }
    void setGetter(ESTree::FunctionExpressionNode *get) {
      if (!isAccessor) {
        valueNode = nullptr;
        setterNode = nullptr;
        isAccessor = true;
      }
      getterNode = get;
    }
    void setSetter(ESTree::FunctionExpressionNode *set) {
      if (!isAccessor) {
        valueNode = nullptr;
        getterNode = nullptr;
        isAccessor = true;
      }
      setterNode = set;
    }
  };

  // First accumulate all getters and setters. We walk all properties, convert
  // them to string and store the value into propMap, possibly overwriting the
  // previous value.
  llvm::StringMap<PropertyValue> propMap;
  llvm::SmallVector<char, 32> stringStorage;

  for (auto &P : Expr->_properties) {
    // We are reusing the storage, so make sure it is cleared at every
    // iteration.
    stringStorage.clear();

    auto *prop = cast<ESTree::PropertyNode>(&P);
    PropertyValue *propValue =
        &propMap[propertyKeyAsString(stringStorage, prop->_key)];
    if (prop->_kind->str() == "get") {
      propValue->setGetter(cast<ESTree::FunctionExpressionNode>(prop->_value));
    } else if (prop->_kind->str() == "set") {
      propValue->setSetter(cast<ESTree::FunctionExpressionNode>(prop->_value));
    } else {
      assert(prop->_kind->str() == "init" && "invalid PropertyNode kind");
      propValue->setValue(prop->_value);
    }
  }

  // Allocate a new javascript object on the heap.
  auto Obj = Builder.createAllocObjectInst(propMap.size());

  // Initialize all properties. We check whether the value of each property
  // will be overwritten (by comparing against what we have saved in propMap).
  // In that case we still compute the value (it could have side effects), but
  // we don't store it. The exception to this are accessor functions - there
  // is no need to create them if we don't use them because creating a function
  // has no side effects.
  for (auto &P : Expr->_properties) {
    // We are reusing the storage, so make sure it is cleared at every
    // iteration.
    stringStorage.clear();

    auto *prop = cast<ESTree::PropertyNode>(&P);
    StringRef keyStr = propertyKeyAsString(stringStorage, prop->_key);
    PropertyValue *propValue = &propMap[keyStr];
    auto *Key = Builder.getLiteralString(keyStr);

    if (prop->_kind->str() == "get" || prop->_kind->str() == "set") {
      // If the property ended up not being a getter/setter, or if we already
      // generated it, skip.
      if (!propValue->isAccessor || propValue->accessorsGenerated)
        continue;

      Value *getter = Builder.getLiteralUndefined();
      Value *setter = Builder.getLiteralUndefined();

      if (propValue->getterNode) {
        getter = genExpression(
            propValue->getterNode,
            Builder.createIdentifier("get " + keyStr.str()));
      }

      if (propValue->setterNode) {
        setter = genExpression(
            propValue->setterNode,
            Builder.createIdentifier("set " + keyStr.str()));
      }

      Builder.createStoreGetterSetterInst(getter, setter, Obj, Key);

      propValue->accessorsGenerated = true;
    } else {
      // Always generate the values, even if we don't need it, for the side
      // effects.
      auto value =
          genExpression(prop->_value, Builder.createIdentifier(keyStr));

      // Only store the value if it won't be overwritten.
      if (propMap[keyStr].valueNode == prop->_value) {
        Builder.createStoreOwnPropertyInst(value, Obj, Key);
      } else {
        Builder.getModule()->getContext().getSourceErrorManager().warning(
            propMap[keyStr].getSourceRange(),
            Twine("the property \"") + keyStr +
                "\" was set multiple times in the object definition.");

        StringRef note = "Previous definition location was here.";
        Builder.getModule()->getContext().getSourceErrorManager().note(
            prop->getSourceRange(), note);
      }
    }
  }

  // Return the newly allocated object (because this is an expression, not a
  // statement).
  return Obj;
}

Value *ESTreeIRGen::genSequenceExpr(ESTree::SequenceExpressionNode *Sq) {
  Value *result = Builder.getLiteralUndefined();

  // Generate all expressions in the sequence, but take only the last one.
  for (auto &Ex : Sq->_expressions) {
    result = genExpression(&Ex);
  }

  return result;
}

Value *ESTreeIRGen::genUnaryExpression(ESTree::UnaryExpressionNode *U) {
  auto kind = UnaryOperatorInst::parseOperator(U->_operator->str());

  // Handle the delete unary expression. https://es5.github.io/#x11.4.1
  if (kind == UnaryOperatorInst::OpKind::DeleteKind) {
    if (auto *memberExpr =
            dyn_cast<ESTree::MemberExpressionNode>(U->_argument)) {
      DEBUG(dbgs() << "IRGen delete member expression.\n");

      Value *obj = genExpression(memberExpr->_object);
      Value *prop = genMemberExpressionProperty(memberExpr);

      // If this assignment is not the identity assignment ('=') then emit a
      // load-operation-store sequence.
      return Builder.createDeletePropertyInst(obj, prop);
    }

    // Check for "delete identifier". Note that deleting unqualified identifiers
    // is prohibited in strict mode, so that case is handled earlier in the
    // semantic validator. Here we are left to handle the non-strict mode case.
    if (auto *iden = dyn_cast<ESTree::IdentifierNode>(U->_argument)) {
      assert(
          !curFunction()->function->isStrictMode() &&
          "delete identifier encountered in strict mode");
      // Check if this is a known variable.
      Identifier name = getNameFieldFromID(iden);
      auto *var = nameTable_.lookup(name);

      if (!var || isa<GlobalObjectProperty>(var)) {
        // If the variable doesn't exist or if it is global, we must generate
        // a delete global property instruction.
        return Builder.createDeletePropertyInst(
            Builder.getGlobalObject(), Builder.getLiteralString(name));
      } else {
        // Otherwise it is a local variable which can't be deleted and we just
        // return false.
        return Builder.getLiteralBool(false);
      }
    }

    // Generate the code for the delete operand.
    genExpression(U->_argument);

    // Deleting any value or a result of an expression returns True.
    return Builder.getLiteralBool(true);
  }

  // Need to handle the special case of "typeof <undefined variable>".
  if (kind == UnaryOperatorInst::OpKind::TypeofKind) {
    if (auto *id = dyn_cast<ESTree::IdentifierNode>(U->_argument)) {
      Value *argument = genIdentifierExpression(id, true);
      return Builder.createUnaryOperatorInst(argument, kind);
    }
  }

  // Generate the unary operand:
  Value *argument = genExpression(U->_argument);

  if (kind == UnaryOperatorInst::OpKind::PlusKind) {
    return Builder.createAsNumberInst(argument);
  }

  return Builder.createUnaryOperatorInst(argument, kind);
}

Value *ESTreeIRGen::genUpdateExpr(ESTree::UpdateExpressionNode *updateExpr) {
  DEBUG(dbgs() << "IRGen update expression.\n");
  bool isPrefix = updateExpr->_prefix;

  // The operands ++ and -- are equivalent to adding or subtracting the
  // literal 1.
  // See section 12.4.4.1.
  BinaryOperatorInst::OpKind opKind;
  if (updateExpr->_operator->str() == "++") {
    opKind = BinaryOperatorInst::OpKind::AddKind;
  } else if (updateExpr->_operator->str() == "--") {
    opKind = BinaryOperatorInst::OpKind::SubtractKind;
  } else {
    llvm_unreachable("Invalid update operator");
  }

  LReference lref = createLRef(updateExpr->_argument);

  // Load the original value.
  Value *original = lref.emitLoad(Builder);

  // Convert the original value to number. Even on suffix operators we return
  // the converted value.
  original = Builder.createAsNumberInst(original);

  // Create the +1 or -1.
  Value *result = Builder.createBinaryOperatorInst(
      original, Builder.getLiteralNumber(1), opKind);

  // Store the result.
  lref.emitStore(Builder, result);

  // Depending on the prefixness return the previous value or the modified
  // value.
  return (isPrefix ? result : original);
}

Value *ESTreeIRGen::genAssignmentExpr(ESTree::AssignmentExpressionNode *AE) {
  DEBUG(dbgs() << "IRGen assignment operator.\n");

  auto opStr = AE->_operator->str();
  auto AssignmentKind = BinaryOperatorInst::parseAssignmentOperator(opStr);

  LReference lref = createLRef(AE->_left);
  Value *RHS = nullptr;

  Identifier nameHint{};
  if (auto *var = lref.castAsVariable()) {
    nameHint = var->getName();
  } else if (auto *globProp = lref.castAsGlobalObjectProperty()) {
    nameHint = globProp->getName()->getValue();
  }

  if (AssignmentKind != BinaryOperatorInst::OpKind::IdentityKind) {
    // Section 11.13.1 specifies that we should first load the
    // LHS before materializing the RHS. Unlike in C, this
    // code is well defined: "x+= x++".
    // https://es5.github.io/#x11.13.1
    auto V = lref.emitLoad(Builder);
    RHS = genExpression(AE->_right, nameHint);
    RHS = Builder.createBinaryOperatorInst(V, RHS, AssignmentKind);
  } else {
    RHS = genExpression(AE->_right, nameHint);
  }

  lref.emitStore(Builder, RHS);

  // Return the value that we stored as the result of the expression.
  return RHS;
}

Value *ESTreeIRGen::genConditionalExpr(ESTree::ConditionalExpressionNode *C) {
  auto parentFunc = Builder.getInsertionBlock()->getParent();

  PhiInst::ValueListType values;
  PhiInst::BasicBlockListType blocks;

  auto alternateBlock = Builder.createBasicBlock(parentFunc);
  auto consequentBlock = Builder.createBasicBlock(parentFunc);
  auto continueBlock = Builder.createBasicBlock(parentFunc);

  // Implement the ternary operator using control flow. We must use control
  // flow because the expressions may have side effects.
  genExpressionBranch(C->_test, consequentBlock, alternateBlock);

  // The 'then' side:
  Builder.setInsertionBlock(consequentBlock);

  values.push_back(genExpression(C->_consequent));
  blocks.push_back(Builder.getInsertionBlock());
  Builder.createBranchInst(continueBlock);

  // The 'else' side:
  Builder.setInsertionBlock(alternateBlock);
  values.push_back(genExpression(C->_alternate));
  blocks.push_back(Builder.getInsertionBlock());
  Builder.createBranchInst(continueBlock);

  // Continue:
  Builder.setInsertionBlock(continueBlock);
  return Builder.createPhiInst(values, blocks);
}

Value *ESTreeIRGen::genIdentifierExpression(
    ESTree::IdentifierNode *Iden,
    bool afterTypeOf) {
  DEBUG(dbgs() << "Looking for identifier \"" << Iden->_name << "\"\n");

  // 'arguments' is an array-like object holding all function arguments.
  // If one of the parameters is called "arguments" then it shadows the
  // arguments keyword.
  if (Iden->_name->str() == "arguments" &&
      !nameTable_.count(getNameFieldFromID(Iden))) {
    // The first time we encounter 'arguments' we must initialize the
    // arguments object before the entry terminator.
    if (!curFunction()->createdArguments) {
      DEBUG(dbgs() << "Creating arguments object\n");

      IRBuilder::SaveRestore saveBuilder(Builder);
      Builder.setInsertionPoint(curFunction()->entryTerminator);
      Builder.setLocation(Builder.getFunction()->getSourceRange().Start);
      curFunction()->createdArguments = Builder.createCreateArgumentsInst();
    }

    return curFunction()->createdArguments;
  }

  // Lookup variable name.
  auto StrName = getNameFieldFromID(Iden);

  auto *Var = ensureVariableExists(Iden);

  // For uses of undefined as the global property, we make an optimization
  // to always return undefined constant.
  if (isa<GlobalObjectProperty>(Var) && StrName.str() == "undefined") {
    return Builder.getLiteralUndefined();
  }

  DEBUG(
      dbgs() << "Found variable " << StrName << " in function \""
             << (isa<GlobalObjectProperty>(Var) ? StringRef("global")
                                                : cast<Variable>(Var)
                                                      ->getParent()
                                                      ->getFunction()
                                                      ->getInternalNameStr())
             << "\"\n");

  // Typeof <variable> does not throw.
  return emitLoad(Builder, Var, afterTypeOf);
}

Value *ESTreeIRGen::genMetaProperty(ESTree::MetaPropertyNode *MP) {
  // Recognize "new.target"
  if (cast<ESTree::IdentifierNode>(MP->_meta)->_name->str() == "new") {
    if (cast<ESTree::IdentifierNode>(MP->_property)->_name->str() == "target") {
      Value *value;

      if (curFunction()->function->getDefinitionKind() ==
              Function::DefinitionKind::ES6Arrow ||
          curFunction()->function->getDefinitionKind() ==
              Function::DefinitionKind::ES6Method) {
        value = curFunction()->capturedNewTarget;
      } else {
        value = Builder.createGetNewTargetInst();
      }

      // If it is a variable, we must issue a load.
      if (auto *V = dyn_cast<Variable>(value))
        return Builder.createLoadFrameInst(V);

      return value;
    }
  }

  llvm_unreachable("invalid MetaProperty");
}

Value *ESTreeIRGen::genNewExpr(ESTree::NewExpressionNode *N) {
  DEBUG(dbgs() << "IRGen 'new' statement/expression.\n");

  // Implement the new operator.
  // http://www.ecma-international.org/ecma-262/7.0/index.html#sec-new-operator

  Value *callee = genExpression(N->_callee);

  ConstructInst::ArgumentList args;
  for (auto &arg : N->_arguments) {
    args.push_back(genExpression(&arg));
  }

  return Builder.createConstructInst(callee, args);
}

Value *ESTreeIRGen::genLogicalExpression(
    ESTree::LogicalExpressionNode *logical) {
  auto opStr = logical->_operator->str();
  DEBUG(dbgs() << "IRGen of short circuiting: " << opStr << ".\n");

  // True if the operand is And (&&) or False if the operand is Or (||).
  bool isAnd = false;

  if (opStr == "&&") {
    isAnd = true;
  } else if (opStr == "||") {
    isAnd = false;
  } else {
    llvm_unreachable("Invalid update operator");
  }

  // Generate a new temporary stack allocation.
  auto tempVarName = genAnonymousLabelName("logical");
  auto parentFunc = Builder.getInsertionBlock()->getParent();
  auto tempVar = Builder.createAllocStackInst(tempVarName);

  auto evalRHSBlock = Builder.createBasicBlock(parentFunc);
  auto continueBlock = Builder.createBasicBlock(parentFunc);

  auto LHS = genExpression(logical->_left);

  // Store the LHS value of the expression in preparation for the case where we
  // won't need to evaluate the RHS side of the expression.
  Builder.createStoreStackInst(LHS, tempVar);

  // Don't continue if the value is evaluated to true for '&&' or false for
  // '||'. Notice that instead of negating the condition we swap the operands of
  // the branch.
  BasicBlock *T = continueBlock;
  BasicBlock *F = evalRHSBlock;
  if (isAnd) {
    std::swap(T, F);
  }
  Builder.createCondBranchInst(LHS, T, F);

  // Continue the evaluation of the right-hand-side of the expression.
  Builder.setInsertionBlock(evalRHSBlock);
  auto RHS = genExpression(logical->_right);

  // Evaluate the RHS and store the result into the temporary variable.
  Builder.createStoreStackInst(RHS, tempVar);

  // Finally, jump to the continuation block.
  Builder.createBranchInst(continueBlock);

  // Load the content of the temp variable that was set in one of the branches.
  Builder.setInsertionBlock(continueBlock);
  return Builder.createLoadStackInst(tempVar);
}

void ESTreeIRGen::genLogicalExpressionBranch(
    ESTree::LogicalExpressionNode *logical,
    BasicBlock *onTrue,
    BasicBlock *onFalse) {
  auto opStr = logical->_operator->str();
  DEBUG(dbgs() << "IRGen of short circuiting: " << opStr << " branch.\n");

  auto parentFunc = Builder.getInsertionBlock()->getParent();
  auto block = Builder.createBasicBlock(parentFunc);

  if (opStr == "&&") {
    genExpressionBranch(logical->_left, block, onFalse);
  } else if (opStr == "||") {
    genExpressionBranch(logical->_left, onTrue, block);
  } else {
    llvm_unreachable("Invalid update operator");
  }

  Builder.setInsertionBlock(block);
  genExpressionBranch(logical->_right, onTrue, onFalse);
}

} // namespace irgen
} // namespace hermes
