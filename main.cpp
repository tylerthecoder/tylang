#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "KaleidoscopeJIT.h"
#include "codegen.cpp"

enum Token {
  tok_eof = -1,

  //commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr; // holds data if token is tok_identifier
static double NumVal; // holds data if token is tok_number

// gettok = Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip any whitespace
  while (isspace(LastChar))
    LastChar = getchar();

  // Identifier: [a-zA-Z][a-zA-Z0-9]
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;

    return tok_identifier;
  }

  // Number: [0-9.]
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // Comments
  if (LastChar == '#') {
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  if (LastChar == EOF)
    return tok_eof;

  int thisChar = LastChar;
  LastChar = getchar();
  return thisChar;
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
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

// Top-Level Parsing


static void InitializeModuleAndPassManager() {
  // Open a new module
  TheModule = std::make_unique<llvm::Module>("JIFF", TheContext);
  TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());


  // Create a new pass manager attached to it
  TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

  // Do simple "peephole" optimizations and bit-twiddling optzns.
  TheFPM->add(llvm::createInstructionCombiningPass());
  // Reassociate expressions.
  TheFPM->add(llvm::createReassociatePass());
  // Eliminate Common SubExpressions.
  TheFPM->add(llvm::createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  TheFPM->add(llvm::createCFGSimplificationPass());

  TheFPM->doInitialization();

}

static void HandleDefinition() {

  if (auto FnAST = ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition. \n");
    if (auto *FnIR = FnAST->codegen()) {
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");
      TheJIT->addModule(std::move(TheModule));
      InitializeModuleAndPassManager();
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
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
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
      // JIT the module containing the anonymous expression, keeping a handle so
      // we can free it later.
      auto H = TheJIT->addModule(std::move(TheModule));
      InitializeModuleAndPassManager();

      // Search the JIT for the __anon_expr symbol.
      auto ExprSymbol = TheJIT->findSymbol("__anon_expr");
      assert(ExprSymbol && "Function not found");

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native function.
      double (*FP)() = (double (*)())(intptr_t)cantFail(ExprSymbol.getAddress());
      fprintf(stderr, "Evaluated to %f\n", FP());

      // Delete the anonymous expression module from the JIT.
      TheJIT->removeModule(H);

      FnIR->print(llvm::errs());
      // flush stderr
      fprintf(stderr, "\n");

      // Remove the anonymous expression.
      // FnIR->eraseFromParent();
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
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 30;
  BinopPrecedence['*'] = 40;

  // Prime the first token
  fprintf(stderr, "READY> ");
  getNextToken();

  TheJIT = std::make_unique<llvm::orc::KaleidoscopeJIT>();

  InitializeModuleAndPassManager();


  // Run the main "interpreter loop"
  MainLoop();

  return 0;
}
