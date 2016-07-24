#pragma once

#include "MacroRecorder.h"

namespace clang{
  class CompilerInstance;
  class SourceManager;
  class ASTConsumer;

}


clang::ASTConsumer* GetASTConsumer(clang::CompilerInstance& ci, RecorderCollection* recorders, bool Verbose);

