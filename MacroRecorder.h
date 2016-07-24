#pragma once

#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringSet.h"

#include "RecorderCollection.h"

#include <map>
#include <memory>


namespace clang{
  class Lexer;
  class CompilerInstance;
  class SourceManager;
  class Sema;
  class Lexer;
  class Token;
  class FunctionDecl;

  namespace tok {
    enum TokenKind : unsigned short;
  };
};


class MacroRecorder : public clang::PPCallbacks {
public:
  explicit MacroRecorder(clang::CompilerInstance& ci, RecorderCollection* collector, bool verbose);

  void MacroExpands(const clang::Token &MacroNameTok, const clang::MacroDefinition &MD, clang::SourceRange Range, const clang::MacroArgs *Args) override;

private:
  void Parse_StackAlias();
  void Parse_Push();
  void Parse_Record();
  void Parse_Record_GetSetField();
  void Parse_NeedsFlag();
  void Parse_NoExtern();
  void Parse_Module();
  
  bool ParsePushValue(PushEntry& result);
  bool SkipToNextToken(clang::tok::TokenKind endToken);
  
  std::string GetMacroArgs(const clang::Token &macroNameTok, clang::SourceRange& Range);
  bool ParseRecordArgParam(std::string& option);

  static clang::StringRef TokenToStringRef(clang::Token& tok);

  void FinalizeRecorder();
  void SetCurrentEntryInvalid(const char* reason, StringRef fmarg = "");
  
  bool LexExpect(clang::tok::TokenKind token){
    
    if(EndOfMacro){
      return false;
    }
    
    EndOfMacro = MacroLexer->LexFromRawLexer(tok);
    return tok.is(token);
  }

  bool LexExpect(clang::tok::TokenKind token1, clang::tok::TokenKind token2){
    return LexExpect(token1) && LexExpect(token2);
  }

  bool LexExpectEnd(clang::tok::TokenKind token){
    return LexExpect(token) && EndOfMacro;
  }

  bool LexExpectEither(clang::tok::TokenKind token1, clang::tok::TokenKind token2){
    return (tok.is(token1) || tok.is(token2));
  }

  int GetStartingLineNumber(){
    return SM->getExpansionLineNumber(MacroLocation.getBegin());
  }

  RecorderCollection* Collector;

private:
  void SetupKeywords();

  bool Verbose;
  RecordEntry* functionEntry;
  llvm::StringMap<PushEntry> StackAlias;
  llvm::StringSet<> NoExtern;

  int CurrentLine;
  llvm::StringRef CurrentKeyword;
  std::string MacroArgs;
  
  bool EndOfMacro;
  clang::Lexer* MacroLexer;
  clang::Token tok;
  clang::SourceRange MacroLocation;

  clang::SourceManager* SM;
  clang::CompilerInstance* CI;

  static llvm::StringMap<int> KeywordLookup;
};
