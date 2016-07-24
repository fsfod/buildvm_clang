
#include "clang/Lex/PPCallbacks.h"

#include "llvm/Support/CommandLine.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include "clang/Parse/ParseAST.h"

#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/TargetSelect.h"

#include "MacroRecorder.h"
#include "LibRegBuilder.h"
#include "FastFunctionCollector.h"

#include <iostream>
#include <fstream>



//using namespace clang;

RecorderCollection* LJMacros = NULL;


extern void AddClangSystemIncludeArgs(clang::HeaderSearchOptions& headerSearch, const std::string& windowsSDKVer, const char* vsVersion = NULL);


using namespace clang::tooling;
using namespace llvm;
using clang::CompilerInstance;


class ParseLJ : public clang::FrontendAction{

public:
  CompilerInstance* ci;

  ParseLJ(bool verbose) :Verbose(verbose){
  }

  virtual clang::ASTConsumer *CreateASTConsumer(CompilerInstance &CI, StringRef InFile){
    CI.getPreprocessor().addPPCallbacks(new MacroRecorder(CI, LJMacros, Verbose));

    LJMacros->NewSourceFile();

    currentConsumer = GetASTConsumer(CI, LJMacros, Verbose);

    return currentConsumer;
  }

  bool usesPreprocessorOnly() const{
    return false;
  }

  void ExecuteAction() override{
    CompilerInstance &CI = getCompilerInstance();

    if (!CI.hasSema())
      CI.createSema(getTranslationUnitKind(), NULL);

    ParseAST(CI.getSema(), CI.getFrontendOpts().ShowStats, CI.getFrontendOpts().SkipFunctionBodies);
  }

  virtual bool BeginInvocation(CompilerInstance &CI) override{ 

    CI.getPreprocessorOpts().addMacroDef("_CRT_SECURE_NO_WARNINGS");
    
    AddClangSystemIncludeArgs(CI.getHeaderSearchOpts(), "v7.0A", "8.0");//"v7.0A");

    clang::LangOptions* languageOptions = &CI.getLangOpts();
    // languageOptions-
    clang::CompilerInvocation::setLangDefaults(*languageOptions, clang::IK_CXX, clang::LangStandard::lang_cxx03);

#if defined(_MSC_VER)
    languageOptions->MicrosoftMode = true;
    languageOptions->MicrosoftExt = true;
    languageOptions->MSCVersion = 1400;
    languageOptions->MSBitfields = 1;
    languageOptions->DelayedTemplateParsing = 1;
#endif

    return true; 
  }

  void EndSourceFileAction() override{
    return;
  }

private:
  bool Verbose;
  clang::ASTConsumer* currentConsumer;
};

class LJFrontendActionFactory : public FrontendActionFactory {

public:
  LJFrontendActionFactory(bool verboseOutput) : 
    VerboseOutput(verboseOutput){
  }

  virtual clang::FrontendAction *create() { 
    return new ParseLJ(VerboseOutput); 
  }

private:
  bool VerboseOutput;
};

cl::list<std::string> SourcePaths(
  cl::Positional,
  cl::desc("<source0> [... <sourceN>]"),
  cl::OneOrMore);

cl::list<std::string> IncludeList(
  "includes",
  cl::CommaSeparated,
  cl::desc("<list of headers to include in the generated file>"),
  cl::ZeroOrMore);

cl::opt<std::string> OutputFile(
  "o",
  cl::desc("<output-file-path>"),
  cl::Required);

cl::opt<bool> VerboseOutput(
  "verbose",
  cl::desc("<print verbose info about parsing>"),
  cl::Optional);



int main(int argc, const char **argv, char * const *envp){

  llvm::OwningPtr<CompilationDatabase> Compilations(FixedCompilationDatabase::loadFromCommandLine(argc, argv));
  cl::ParseCommandLineOptions(argc, argv);

  if(!Compilations){
    std::string ErrorMessage;
    Compilations.reset(CompilationDatabase::autoDetectFromSource(SourcePaths[0], ErrorMessage));
    
    if(!Compilations)llvm::report_fatal_error(ErrorMessage);
  }

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();

  ClangTool Tool(*Compilations, SourcePaths);

  LJMacros = new RecorderCollection(VerboseOutput);

  Tool.run(new LJFrontendActionFactory(VerboseOutput));

  LibRegBuilder regBuilder(LJMacros, OutputFile);

  if(!regBuilder.RecordersValid()){
    return 1;
  }

  regBuilder.WriteLibReg(IncludeList);

  std::cout.flush();

  return 0;
}