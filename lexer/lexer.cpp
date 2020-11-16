#include <string>
#include "lexer.h"

int Lexer::getNextChar() {
  return getc(fp);
}

int Lexer::gettok() {
  static int LastChar = ' ';

  // Skip any whitespace
  while (isspace(LastChar))
    LastChar = getNextChar();

  // Identifier: [a-zA-Z][a-zA-Z0-9]
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getNextChar())))
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
      LastChar = getNextChar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // Comments
  if (LastChar == '#') {
    do
      LastChar = getNextChar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  if (LastChar == EOF)
    return tok_eof;

  int thisChar = LastChar;
  LastChar = getNextChar();
  return thisChar;
}

int Lexer::getNextToken() {
  return CurTok = gettok();
}

std::string Lexer::getIdentifierStr() {
  return IdentifierStr;
}

int Lexer::getCurrentToken() {
  return CurTok;
}

int Lexer::getNumberVal() {
  return NumVal;
}
