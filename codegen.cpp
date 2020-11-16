#include "ast.h"

static llvm::LLVMContext TheContext;
static llvm::IRBuilder<> Builder(TheContext);
static std::unique_ptr<llvm::Module> TheModule;
static std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
static std::map<std::string, llvm::Value *> NamedValues;
static std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

// Helpers
llvm::Value *LogErrorV(const char *Str) {
  LogErrorV(Str);
  return nullptr;
}

llvm::Function *getFunction(std::string funcName) {
  // first check to see if we already have the module
  if (auto *F = TheModule->getFunction(funcName))
    return F;

  // If not, check where we can codegen the declaration for some existing proto
  auto FI = FunctionProtos.find(funcName);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // if no existing prototype
  return nullptr;
}

// codegen

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(TheContext, llvm::APFloat(val));
}

llvm::Value *VariableExprAST::codegen() {
  // Look this variable up in function
  llvm::Value *V = NamedValues[name];
  if (!V) {
    LogErrorV("Unknown variable name");
  }
  return V;
}

llvm::Value *BinaryExprAST::codegen() {
  llvm::Value *L = LHS->codegen();
  llvm::Value *R = RHS->codegen();
  if (!L || !R) {
    return nullptr;
  }

  switch (op)
  {
  case '+':
    return Builder.CreateFAdd(L, R, "addtmp");
    break;
  case '-':
    return Builder.CreateFSub(L, R, "subtmp");
    break;
  case '*':
    return Builder.CreateFMul(L, R, "multmp");
    break;
  case '<':
    L = Builder.CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder.CreateUIToFP(L, llvm::Type::getDoubleTy(TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

llvm::Function *PrototypeAST::codegen() {
  // Make the function type: double(double, double) etc.

  std::vector<llvm::Type*> Doubles(args.size(), llvm::Type::getDoubleTy(TheContext));

  llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(TheContext), Doubles, false);
  llvm::Function *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, name, TheModule.get());

  // Set names for all arguments
  unsigned idx = 0;
  for (auto &arg : F->args()) {
    arg.setName(args[idx++]);
  }

  return F;
}

llvm::Value *CallExprAST::codegen() {
  // Look up the name in the global module table
  llvm::Function *CalleeF = getFunction(callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != args.size())
    return LogErrorV("Incorrect number of arguments");

  std::vector<llvm::Value *> ArgsV;
  for (unsigned i = 0, e = args.size(); i != e; ++i) {
      ArgsV.push_back(args[i]->codegen());
      if (!ArgsV.back())
        return nullptr;
  }

  return Builder.CreateCall(CalleeF, ArgsV, "calltemp");
}


llvm::Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto &P = *proto;
  FunctionProtos[proto->getName()] = std::move(proto);
  llvm::Function *theFunction = getFunction(P.getName());
  if (!theFunction)
    return nullptr;

  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(TheContext, "entry", theFunction);
  Builder.SetInsertPoint(BB);

  // Record the function arguments in the Named Values map.
  NamedValues.clear();
  for (auto &arg : theFunction->args())
    NamedValues[arg.getName()] = &arg;

  llvm::Value *RetVal = body->codegen();

  if (RetVal) {
    // finish off the function
    Builder.CreateRet(RetVal);

    //validate the generated code, checking for consistency.
    llvm::verifyFunction(*theFunction);

    TheFPM->run(*theFunction);

    return theFunction;
  }

  // error reading body,
  theFunction->eraseFromParent();
  return nullptr;
}