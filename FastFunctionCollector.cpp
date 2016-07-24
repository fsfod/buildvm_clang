
#include "clang/ASTMatchers/ASTMatchers.h"
#pragma hdrstop

#include "FastFunctionCollector.h"
#include "clang/Tooling/Tooling.h"

#include "clang/AST/Decl.h"
#include "clang/AST/PrettyPrinter.h"

#include "clang/Frontend/CompilerInstance.h"

#include "ASTMatchFinder.h"
#include "RecorderCollection.h"

#include <iostream>

using clang::ast_matchers::MatchFinder;


class FunctionMatchCallback : public MatchFinder::MatchCallback {
public:
  FunctionMatchCallback(clang::SourceManager& sm, RecorderCollection* recorders, bool verbose) 
    :SM(sm), Recorders(recorders), Verbose(verbose){
  }

  virtual void run(const MatchFinder::MatchResult &Result) {

    auto const* func = Result.Nodes.getDeclAs<clang::FunctionDecl>("id");

    if(Verbose){
      int line = SM.getExpansionLineNumber(func->getLocStart());
      auto name = func->getDeclName().getAsString();

      std::cout << "ASTWalker: found function " << line << ": " << name << std::endl << std::endl;
    }

    Recorders->LuaCFunctionDefined(func);
  }

  virtual void onStartOfTranslationUnit() {

  }

  static void CreateMatcher(){
  
  }

private:
  bool Verbose;
  RecorderCollection* Recorders;
  clang::SourceManager& SM;
};

clang::ASTConsumer* GetASTConsumer(clang::CompilerInstance& ci, RecorderCollection* recorders, bool Verbose){

  using namespace clang::ast_matchers;    
  
  auto m = functionDecl(parameterCountIs(1),
           hasParameter(0, hasType(pointsTo(recordDecl(hasName("lua_State"))))), 
           returns(asString("int"))).bind("id");

  auto consumer = new MatchASTConsumer(ci.getASTContext());

  consumer->addMatcher(m, new FunctionMatchCallback(ci.getSourceManager(), recorders, Verbose));

  return consumer;
}



