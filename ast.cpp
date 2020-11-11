#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "main.cpp"



static std::unique_ptr<llvm::LLVMContext> TheContext;
static std::unique_ptr<llvm::Module> TheModule;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::map<std::string, llvm::Value *> NamedValues;

llvm::Value *LogErrorV(const char *Str) {
  LogErrorV(Str);
  return nullptr;
}

class ExprAST {
public:
  virtual ~ExprAST() {}
  virtual llvm::Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double val;
public:
  NumberExprAST(double val) : val(val) {}
  virtual llvm::Value *codegen();
};

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(*TheContext, llvm::APFloat(val));
}

class VariableExprAST : public ExprAST {
  std::string name;
public:
  VariableExprAST(const std::string &name): name(name) {}
  virtual llvm::Value *codegen();
};

llvm::Value *VariableExprAST::codegen() {
  // Look this variable up in function
  llvm::Value *V = NamedValues[name];
  if (!V) {
    LogErrorV("Unknown variable name");
  }
  return V;
}

class BinaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> LHS, RHS;
public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS): op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  virtual llvm::Value *codegen();
};

llvm::Value *BinaryExprAST::codegen() {
  llvm::Value *L = LHS->codegen();
  llvm::Value *R = RHS->codegen();
  if (!L || !R) {
    return nullptr;
  }

  switch (op)
  {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
    break;
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
    break;
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
    break;
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

class CallExprAST : public ExprAST {
  std::string callee;
  std::vector<std::unique_ptr<ExprAST>> args;
public:
  CallExprAST(const std::string &callee, std::vector<std::unique_ptr<ExprAST>> args): callee(callee), args(std::move(args)) {}
  virtual llvm::Value *codegen();
};

llvm::Value *CallExprAST::codegen() {
  // Look up the name in the global module table
  llvm::Function *CalleeF = TheModule->getFunction(callee);
  if (!CalleeF)
    return LogErrorV("Unkown function referenced");

  // If argument mismatch error.
  if (CalleeF->arg_size() != args.size())
    return LogErrorV("Incorrect number of arguments");

  std::vector<llvm::Value *> ArgsV;
  for (unsigned i = 0, e = args.size(); i != e; ++i) {
      ArgsV.push_back(args[i]->codegen());
      if (!ArgsV.back())
        return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltemp");
}


class PrototypeAST {
  std::string name;
  std::vector<std::string> args;

public:
  PrototypeAST(const std::string &name, std::vector<std::string> args): name(name), args(std::move(args)) {}
  const std::string &getName() const { return name; }
  virtual llvm::Function *codegen();
};

llvm::Function *PrototypeAST::codegen() {
  // Make the function type: double(double, double) etc.

  std::vector<llvm::Type*> Doubles(args.size(), llvm::Type::getDoubleTy(*TheContext));

  llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext), Doubles, false);
  llvm::Function *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, name, TheModule.get());

  // Set names for all arguments
  unsigned idx = 0;
  for (auto &arg : F->args()) {
    arg.setName(args[idx++]);
  }

  return F;
}

class FunctionAST {
  std::unique_ptr<PrototypeAST> proto;
  std::unique_ptr<ExprAST> body;
public:
  FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) : proto(std::move(proto)), body(std::move(body)) {}
  virtual llvm::Function *codegen();
};

llvm::Function *FunctionAST::codegen() {
  // First, check for an existing function from a previous 'extern' declaration
  llvm::Function *theFunction = TheModule->getFunction(proto->getName());
  if (!theFunction)
    theFunction = proto->codegen();

  if (!theFunction)
    return nullptr;

  if (!theFunction->empty())
    return (llvm::Function*) LogErrorV("Function cannot be redefined");

  // Create a new basic block to start insertion into.
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", theFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the Named Values map.
  NamedValues.clear();
  for (auto &arg : theFunction->args())
    NamedValues[arg.getName()] = &arg;

  if (llvm::Value *RetVal = body->codegen()) {
    // finish off the function
    Builder->CreateRet(RetVal);

    //validate the generated code, checking for consistency.
    llvm::verifyFunction(*theFunction);

    return theFunction;
  }

  // error reading body,
  theFunction->eraseFromParent();
  return nullptr;
}

static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}

std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "LogError: %s\n" , Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *str) {
  LogError(str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();


// numberexpr ::= number
// called when the current token is a number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(result);
}

// parenexpr ::= '(' expression ')'
// called when the current token is a (
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat the '('

  auto V = ParseExpression();

  if (!V) return nullptr;

  if (CurTok != ')')
    return LogError("expected ')'");

  getNextToken(); // eat ).

  return V;
}

// ::= identifer
// ::= identifer '(' expression ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string idName = IdentifierStr;

  getNextToken(); // eat the identifier

  if (CurTok != '(')
    return std::make_unique<VariableExprAST>(idName);

  // Call
  getNextToken(); // eat (
  std::vector<std::unique_ptr<ExprAST>> args;
  if (CurTok != ')') {
    while (1) {
      if (auto arg = ParseExpression())
        args.push_back(std::move(arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");

      getNextToken();
    }
  }

  getNextToken(); // Eat the ')'

  return std::make_unique<CallExprAST>(idName, std::move(args));
}

// Primary
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch(CurTok) {
  default:
    return LogError("Unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  }
}


static std::map<char, int> BinopPrecedence;

static int getTokenPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it is in the bin op map
  int prec = BinopPrecedence[CurTok];
  if (prec <=0 ) return -1;

  return prec;
}


static std::unique_ptr<ExprAST> ParseBinOpRHS(int exprPrec, std::unique_ptr<ExprAST> LHS) {
  // if this is a binop, find its precedence
  while (1) {
    int tokPrec = getTokenPrecedence();

    if (tokPrec < exprPrec)
      return LHS;

    int binOp = CurTok;
    getNextToken(); // eat binop

    // Parse the primary expr after the binary operator
    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    int nextPrec = getTokenPrecedence();
    if (tokPrec < nextPrec) {
      RHS = ParseBinOpRHS(tokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    LHS = std::make_unique<BinaryExprAST>(binOp, std::move(LHS), std::move(RHS));

  }

}

static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  std::string funcName = IdentifierStr;
  getNextToken();

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype but found");

  std::vector<std::string> argNames;
  while (getNextToken() == tok_identifier)
    argNames.push_back(IdentifierStr);

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

  getNextToken(); // eat )

  return std::make_unique<PrototypeAST>(funcName, std::move(argNames));
}


static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat def
  auto Proto = ParsePrototype();
  if (!Proto) return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

  return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern
  return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    // Make an anonymouse proto.
    auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

// Top-Level Parsing


static void InitializeModule() {
  // Open a new context and module.
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("JIFF", *TheContext);

  // Create a new builder for the module.
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}

//
static void HandleDefinition() {

  if (auto FnAST = ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition. \n");
    if (auto *FnIR = FnAST->codegen()) {
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    fprintf(stderr, "Parsed an extern \n");
    if (auto *FnIR = ProtoAST->codegen()) {
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function
  if (auto FnAST = ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr \n");
    if (auto *FnIR = FnAST->codegen()) {
      FnIR->print(llvm::errs());
      // flush stderr
      fprintf(stderr, "\n");

      // Remove the anonymous expression.
      FnIR->eraseFromParent();
    }
  } else {
    getNextToken();
  }
}

static void MainLoop() {
  while(1) {
    fprintf(stderr, "READY> ");
    switch(CurTok){
      case tok_eof:
        return;
      case ';': // ignore top-level semicolons.
        getNextToken();
        break;
      case tok_def:
        HandleDefinition();
        break;
      case tok_extern:
        HandleExtern();
        break;
      default:
        HandleTopLevelExpression();
        break;
    }

  }
}

int main() {
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 30;
  BinopPrecedence['*'] = 40;

  // Prime the first token
  fprintf(stderr, "READY> ");
  getNextToken();

  // build the llvm context
  InitializeModule();

  // Run the main "interpreter loop"
  MainLoop();

  return 0;
}
