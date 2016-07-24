#pragma once

#include "clang/Lex/PPCallbacks.h"
#include "ClangFrontEnd.h"

namespace clang{
  class Lexer;
};

enum PushType{
  PushType_Invalid = 0,
  PushType_String,
  PushType_StackSlot,
  PushType_StackSlotBase,
  PushType_MemberTable,
};

class PushEntry{

public:
  PushEntry() : Type(PushType_Invalid), StackSlot(0){
  }

  PushEntry(const clang::StringRef& strLiteral){
    StringLiteral = new std::string(strLiteral);
    Type = PushType_String;
  }
  
  PushEntry(PushType typeNum) : Type(typeNum), StackSlot(0){
  }

  PushEntry(PushType typeNum, int stackSlot): Type(typeNum), StackSlot(stackSlot){
  }

  PushEntry(int stackSlot){
    StackSlot = stackSlot;
    Type = PushType_StackSlot;
  }

public:
  PushType Type;
  union{
    std::string* StringLiteral;
    int StackSlot;
  };
};

class RecordEntry{

public:
  int FunctionId;
  std::string RecordOptions;
  std::string Name;
  std::string TraceRecorder;
  int RecordLineNumber;
  std::vector<PushEntry> PushStack;
};

class ObjectData{
public:
  std::vector<RecordEntry*> MemberFunctions;
  std::vector<RecordEntry*> MetaFunctions;
};


class MacroCollector{

public:
  explicit MacroCollector();
  ~MacroCollector();
  void SetCompilerInstance(clang::CompilerInstance& ci);

  void ProcessStackAlias(clang::Lexer& lexer);
  
  void ProcessPush(clang::Lexer& lexer, const clang::Token &MacroNameTok, clang::SourceRange range);
  void ProcessRecord(clang::Lexer& lexer, const clang::Token &MacroNameTok, clang::SourceRange range);

  static clang::StringRef TokenToStringRef(clang::Token& tok);
  
  ObjectData* GetFunctionList(std::string& objectName);

  void SetInModule(clang::StringRef& name){
    InModule = true;
  }

public:
  RecordEntry* functionEntry;
  std::vector<RecordEntry*> GobalFunctions;
  std::vector<RecordEntry*> AllFunctions;
  std::map<std::string, ObjectData*> ObjectFunctions;

private:
  friend class MacroRecorder;
  PushEntry ParsePushValue(clang::Lexer& lexer);
  clang::SourceManager* SM;
  
  int FunctionId;
  bool InModule;
  
  llvm::StringMap<PushEntry> StackAlias;
};

class MacroRecorder : public clang::PPCallbacks {
public:
  explicit MacroRecorder(clang::CompilerInstance& ci, MacroCollector* collector) : 
      CI(&ci), SM(&ci.getSourceManager()), Collector(collector) {
   
    collector->SetCompilerInstance(ci);
  }

  std::string GetMacroArgs(const clang::Token &MacroNameTok, clang::SourceRange& Range);
  
  
  void MacroExpands(const clang::Token &macroNameTok, const clang::MacroInfo* MI, clang::SourceRange range);
  void MacroDefined(const clang::Token &macroNameTok, const clang::MacroInfo* MI);

private:
  MacroCollector* Collector;
  clang::SourceManager* SM;
  clang::CompilerInstance* CI;
};
