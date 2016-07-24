
#include "clang/Lex/PPCallbacks.h"
#include "ClangFrontEnd.h"
#include "clang/AST/ASTConsumer.h"

#include "llvm/Support/CommandLine.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Parse/ParseAST.h"

#include "clang/Sema/Sema.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"

#include "MacroRecorder.h"
#include "LibRegBuilder.h"
#include <iostream>
#include <fstream>

using std::string;
using namespace clang;


class MyASTConsumer : public clang::ASTConsumer{

public:
    MyASTConsumer() : clang::ASTConsumer() { }
    virtual ~MyASTConsumer() { }

    virtual bool HandleTopLevelDecl( clang::DeclGroupRef d)
    {
        static int count = 0;
        clang::DeclGroupRef::iterator it;
        for( it = d.begin(); it != d.end(); it++)
        {
            count++;
       
            if(clang::VarDecl *vd = llvm::dyn_cast<clang::VarDecl>(*it)){
             
              if(vd->isFileVarDecl() && !vd->hasExternalStorage()){
              //  std::cerr << "Read top-level variable decl: '";
              //  std::cerr << vd->getDeclName().getAsString() ;
              //  std::cerr << std::endl;
              }
            }else{

              if(FunctionDecl *func = llvm::dyn_cast<FunctionDecl>(*it)){
                std::string name = func->getDeclName().getAsString() ;

                if(name.find("lj_cf") != std::string::npos){
                   std::cerr << "ASTWalker: found function " << name << std::endl << std::endl;
                }

             // }else if(FunctionDecl *func = llvm::dyn_cast<call>(*it)){
              }

              continue;
            }


        }
        return true;
    }
};


extern void AddClangSystemIncludeArgs(clang::HeaderSearchOptions& headerSearch, const std::string& windowsSDKVer);

void pushValueExternal(const PushEntry& pushValue, std::ostream& output){
  
  if(pushValue.Type == PushType_String) {
    output << "  lua_pushstring(L, \"" << (*pushValue.StringLiteral) << "\"));\n";
  }else if(pushValue.Type == PushType_MemberTable){
    output << "  lua_pushvalue(L, memberTableIndex);\n";
  }else{
    output << "  lua_pushvalue(L, " << pushValue.StackSlot << ");\n";
  }
}



using namespace clang::tooling;
using namespace llvm;

MacroCollector* LJMacros = NULL;

class ParseLJ : public FrontendAction{

public:
  CompilerInstance* ci;

  ParseLJ(){
  }

  virtual ASTConsumer *CreateASTConsumer(CompilerInstance &CI, StringRef InFile){
    CI.getPreprocessor().addPPCallbacks(new MacroRecorder(CI, LJMacros));

    return new MyASTConsumer();
  }

  bool usesPreprocessorOnly() const{
    return false;
  }

  void ExecuteAction() {
    CompilerInstance &CI = getCompilerInstance();

    if (!CI.hasSema())
      CI.createSema(getTranslationUnitKind(), NULL);

    ParseAST(CI.getSema(), CI.getFrontendOpts().ShowStats, CI.getFrontendOpts().SkipFunctionBodies);
  }

  virtual bool BeginInvocation(CompilerInstance &CI){ 

    CI.getPreprocessorOpts().addMacroDef("_CRT_SECURE_NO_WARNINGS");

    clang::LangOptions* languageOptions = &CI.getLangOpts();
    // languageOptions-
    CompilerInvocation::setLangDefaults(*languageOptions, clang::IK_CXX, clang::LangStandard::lang_cxx11);

#if defined(_MSC_VER)
    languageOptions->MicrosoftMode = true;
    languageOptions->MicrosoftExt = true;
    languageOptions->MSCVersion = _MSC_VER;
    languageOptions->MSBitfields = 1;
    languageOptions->DelayedTemplateParsing = 1;
#endif

    return true; 
  }

  virtual void EndSourceFileAction(){
  }
};



cl::list<std::string> SourcePaths(
  cl::Positional,
  cl::desc("<source0> [... <sourceN>]"),
  cl::OneOrMore);

cl::opt<std::string> OutputFile(
  "o",
  cl::desc("<output-file-path>"),
  cl::Required);

int main(int argc, const char **argv, char * const *envp){

  llvm::OwningPtr<CompilationDatabase> Compilations(FixedCompilationDatabase::loadFromCommandLine(argc, argv));
  cl::ParseCommandLineOptions(argc, argv);

  if(!Compilations){
    std::string ErrorMessage;
    Compilations.reset(CompilationDatabase::autoDetectFromSource(SourcePaths[0], ErrorMessage));
    
    if(!Compilations)llvm::report_fatal_error(ErrorMessage);
  }

  ClangTool Tool(*Compilations, SourcePaths);

  LJMacros = new MacroCollector();

  Tool.run(newFrontendActionFactory<ParseLJ>());

  LibRegBuilder regBuilder(LJMacros, OutputFile);

  regBuilder.WriteLibReg();

  return 0;
}