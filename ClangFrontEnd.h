//
// ===============================================================================
// clReflect, ClangFrontend.h - All code required to initialise and control
// the clang frontend.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once

#include "llvm/Support/raw_ostream.h"

#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/CompilerInstance.h"


struct Arguments;

namespace clang{
  class PPCallbacks;
};

//
// Parse a file token stream, building a clang AST Context
// This context can then be used to walk the AST as many times as needed
//
class ClangParser{

public:
  ClangParser();
  enum HeaderType
  {
          HeaderType_User,
          HeaderType_System,
          HeaderType_ExternC
  };
  
  bool ParseAST(const char* filename);
  
  void GetIncludedFiles(std::vector< std::pair<HeaderType,std::string> >& files) const;
  
  clang::ASTContext& GetASTContext() { return CompilerInstance.getASTContext(); }

  void SetPPCallbacks(clang::PPCallbacks* callbacks);

  void SetASTConsumer(clang::ASTConsumer* ast_Consumer){
    ASTConsumer = ast_Consumer;
  }

  clang::HeaderSearchOptions& getHeaderSearchOpts(){
    return CompilerInstance.getHeaderSearchOpts();
  }

  void AddIncludePath(clang::StringRef Path, clang::frontend::IncludeDirGroup Group, bool IsUserSupplied = false, bool IsFramework = false, bool IgnoreSysRoot = false,
                      bool IsInternal = false, bool ImplicitExternC = false);


public:
  clang::CompilerInstance CompilerInstance;

private:
  clang::PPCallbacks* PrepocessorCallbacks;

  llvm::raw_fd_ostream OutputStream;
  clang::DiagnosticOptions DiagnosticOptions;
  llvm::OwningPtr<clang::CompilerInvocation> CompilerInvocation;
  llvm::OwningPtr<clang::TargetInfo> TargetInfo;

  clang::ASTConsumer* ASTConsumer;
};