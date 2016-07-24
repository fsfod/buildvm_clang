
#pragma once

#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <Vector>

namespace clang{
  
class ASTContext;


namespace ast_matchers{

namespace internal{
  class DynTypedMatcher;
  class MatchCallback;
}


class MatchASTConsumer : public ASTConsumer{
public:
  MatchASTConsumer(clang::ASTContext& sm);
  ~MatchASTConsumer();

  void addMatcher(const DeclarationMatcher &NodeMatch, MatchFinder::MatchCallback *Action);
  void addMatcher(const TypeMatcher &NodeMatch, MatchFinder::MatchCallback *Action);
  void addMatcher(const StatementMatcher &NodeMatch, MatchFinder::MatchCallback *Action);
  void addMatcher(const NestedNameSpecifierMatcher &NodeMatch, MatchFinder::MatchCallback *Action);
  void addMatcher(const NestedNameSpecifierLocMatcher &NodeMatch, MatchFinder::MatchCallback *Action);
  void addMatcher(const TypeLocMatcher &NodeMatch, MatchFinder:: MatchCallback *Action);

  bool addDynamicMatcher(const internal::DynTypedMatcher & NodeMatch, MatchFinder::MatchCallback * Action);

private:
  /*Visit declarations as there parsed instead after the whole file is parsed loading macro context */
  bool HandleTopLevelDecl(clang::DeclGroupRef d) override;
  void HandleTranslationUnit(ASTContext &Context) override;

  void* Visitor;
  clang::ASTContext& ActiveASTContext;
  MatchFinder::MatchersByType Matchers;
  MatchFinder::ParsingDoneTestCallback *ParsingDone;
};

}
}