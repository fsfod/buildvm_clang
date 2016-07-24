
#include "ClangFrontEnd.h"

#include "llvm/Support/Host.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"       
#include "clang/Frontend/LangStandard.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Sema/Sema.h"

struct EmptyASTConsumer : public clang::ASTConsumer{
  virtual ~EmptyASTConsumer() { }
  virtual bool HandleTopLevelDecl(clang::DeclGroupRef D){ 
    return true; 
  }
};

ClangParser::ClangParser()
        // VC2005: If shouldClose is set to true, this forces an assert in the CRT on program
        // shutdown as stdout hasn't been opened by the app in the first place.
        : OutputStream(1, false), ASTConsumer(NULL){

  CompilerInvocation.reset(new clang::CompilerInvocation);

  CompilerInvocation->getPreprocessorOpts().addMacroDef("_CRT_SECURE_NO_WARNINGS");

  clang::LangOptions* languageOptions = CompilerInvocation->getLangOpts();
  CompilerInvocation->setLangDefaults(*languageOptions, clang::IK_CXX, clang::LangStandard::lang_cxx11);

  //CompilerInvocation->setLangDefaults(*languageOptions, clang::IK_C, clang::LangStandard::lang_c99);

  languageOptions->Bool = true;

#if defined(_MSC_VER)
  languageOptions->MicrosoftMode = true;
  languageOptions->MicrosoftExt = true;
  languageOptions->MSCVersion = _MSC_VER;
  languageOptions->MSBitfields = 1;
  languageOptions->DelayedTemplateParsing = 1;
#endif

// Setup diagnostics output; MSVC line-clicking and suppress warnings from system headers
#if defined(_MSC_VER)
  DiagnosticOptions.setFormat(DiagnosticOptions.Msvc);
#else
  DiagnosticOptions.Format = DiagnosticOptions.Clang;
#endif  // CLCPP_USING_MSVC

  clang::TextDiagnosticPrinter *client = new clang::TextDiagnosticPrinter(OutputStream, DiagnosticOptions);
  CompilerInstance.createDiagnostics(0, NULL, client);
  CompilerInstance.getDiagnostics().setSuppressSystemWarnings(true);

  // Setup target options - ensure record layout calculations use the MSVC C++ ABI
  clang::TargetOptions& targetOptions = CompilerInvocation->getTargetOpts();
  targetOptions.Triple = llvm::sys::getDefaultTargetTriple();

#if defined(_MSC_VER)
  targetOptions.CXXABI = "microsoft";
#else

#endif

  TargetInfo.reset(clang::TargetInfo::CreateTargetInfo(CompilerInstance.getDiagnostics(), targetOptions));
  CompilerInstance.setTarget(TargetInfo.take());

  //CompilerInvocation->CreateFromArgs

  // Set the invokation on the instance
  CompilerInstance.createFileManager();
  CompilerInstance.createSourceManager(CompilerInstance.getFileManager());
  CompilerInstance.setInvocation(CompilerInvocation.take());
}

bool ClangParser::ParseAST(const char* filename){

  // Recreate preprocessor and AST context
  CompilerInstance.createPreprocessor();
  CompilerInstance.createASTContext(); 
  CompilerInstance.getSourceManager().clearIDTables();

  //CompilerInstance.createFileManager();
  //CompilerInstance.createSourceManager(CompilerInstance.getFileManager());

  CompilerInstance.getPreprocessor().addPPCallbacks(PrepocessorCallbacks);

  // Initialize builtins
  if(CompilerInstance.hasPreprocessor()) {
    clang::Preprocessor& preprocessor = CompilerInstance.getPreprocessor();
    preprocessor.getBuiltinInfo().InitializeBuiltins(preprocessor.getIdentifierTable(), preprocessor.getLangOpts());
  }

  // Get the file  from the file system
  const clang::FileEntry* file = CompilerInstance.getFileManager().getFile(filename);
  CompilerInstance.getSourceManager().createMainFileID(file);

  if(ASTConsumer == NULL){
    ASTConsumer = new EmptyASTConsumer();
  }

  
  // Parse the AST
  clang::DiagnosticConsumer* client = CompilerInstance.getDiagnostics().getClient();
  client->BeginSourceFile(CompilerInstance.getLangOpts(), &CompilerInstance.getPreprocessor());
  clang::ParseAST(CompilerInstance.getPreprocessor(), ASTConsumer, CompilerInstance.getASTContext());
  client->EndSourceFile();

  CompilerInstance.takeASTConsumer();
  CompilerInstance.takeSema();
  CompilerInstance.resetAndLeakASTContext();

  CompilerInstance.getPreprocessor().EndSourceFile();

  return client->getNumErrors() == 0;
}

void ClangParser::SetPPCallbacks(clang::PPCallbacks* callbacks){
  PrepocessorCallbacks = callbacks;
}

void ClangParser::AddIncludePath(clang::StringRef Path, clang::frontend::IncludeDirGroup Group, bool IsUserSupplied, bool IsFramework, bool IgnoreSysRoot,
                                 bool IsInternal, bool ImplicitExternC){
    
  CompilerInstance.getHeaderSearchOpts().AddPath(Path, Group, IsUserSupplied, IsFramework, IgnoreSysRoot, IsInternal, ImplicitExternC);
}