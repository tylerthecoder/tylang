#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"

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

class VariableExprAST : public ExprAST {
  std::string name;
public:
  VariableExprAST(const std::string &name): name(name) {}
  virtual llvm::Value *codegen();
};

class BinaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> LHS, RHS;
public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS): op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  virtual llvm::Value *codegen();
};

class CallExprAST : public ExprAST {
  std::string callee;
  std::vector<std::unique_ptr<ExprAST>> args;
public:
  CallExprAST(const std::string &callee, std::vector<std::unique_ptr<ExprAST>> args): callee(callee), args(std::move(args)) {}
  virtual llvm::Value *codegen();
};

class PrototypeAST {
  std::string name;
  std::vector<std::string> args;

public:
  PrototypeAST(const std::string &name, std::vector<std::string> args): name(name), args(std::move(args)) {}
  const std::string &getName() const { return name; }
  virtual llvm::Function *codegen();
};

class FunctionAST {
  std::unique_ptr<PrototypeAST> proto;
  std::unique_ptr<ExprAST> body;
public:
  FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body) : proto(std::move(proto)), body(std::move(body)) {}
  virtual llvm::Function *codegen();
};