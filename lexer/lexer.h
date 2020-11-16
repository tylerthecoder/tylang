#include <string>

enum Token {
  tok_eof = -1,

  //commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

class Lexer {
public:
  Lexer() {}
  Lexer(FILE * fp) : fp(fp) {}
  int getNextToken();
  std::string getIdentifierStr();
  int getCurrentToken();
  int getNumberVal();

private:
  // holds data if token is tok_identifier
  std::string IdentifierStr;
  // holds data if token is tok_number
  double NumVal;
  FILE * fp;

  int LastChar;
  int CurTok;

  int getNextChar();
  int gettok();
};