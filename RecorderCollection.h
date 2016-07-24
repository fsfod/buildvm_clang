
#pragma once

#include "RecorderEntry.h"
#include <map>
#include <memory>

namespace clang{
  class CompilerInstance;
  class SourceManager;
  class Sema;
  class FunctionDecl;
  struct PrintingPolicy;
};

class RecorderCollection{

public:
  explicit RecorderCollection(bool Verbose);
  ~RecorderCollection();

  void SetCompilerInstance(clang::CompilerInstance& ci);
  
  void NewSourceFile(){
  }

  void SetInModule(StringRef& name){
    InModule = true;
  }

  void ModuleDefined(std::string& name, std::string& moduleType);

  void RecorderFinalized(RecordEntry* recorder);
  void LuaCFunctionDefined(const clang::FunctionDecl *func);

private:
  void RegisterEntryToGroup(RecordEntry* entry);
  void ReportError(const char* fmtmsg, StringRef fmtvalue);
  ObjectRecorderData* GetFunctionList(std::string& objectName);

public:
  std::vector<RecordEntry*> GobalFunctions;
  std::vector<RecordEntry*> AllFunctions;
  std::map<std::string, ObjectRecorderData*> ObjectFunctions;

private:
  bool Verbose;
  clang::CompilerInstance* CI;
  clang::SourceManager* SM;
  
  int FunctionId;
  std::unique_ptr<RecordEntry> UnboundRecorder;

  clang::PrintingPolicy* PrintPolicy;

  bool InModule;
};