
#include "RecorderCollection.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"

#include <iostream>

using namespace clang::sema;

using clang::Sema;
using llvm::dyn_cast;
using std::string;
using std::pair;

clang::ValueDecl* GetFieldInfo(Sema& sema, const clang::DeclContext* context, const StringRef& className, const StringRef& fieldName){
     
  auto& ident = sema.getASTContext().Idents.get(className);

  auto& decName = clang::DeclarationName(&ident);

  auto lookupResukt = context->lookup(decName);

  clang::CXXRecordDecl* cxxRecordPtr = NULL;

  for(auto it = lookupResukt.begin(); cxxRecordPtr == NULL && it != lookupResukt.end(); it++){
     
    auto name = (*it)->getName();
   
    cxxRecordPtr = dyn_cast<clang::CXXRecordDecl>(*it);

    if(cxxRecordPtr != NULL){
      continue;
    }

    auto usingdec = dyn_cast<clang::UsingDecl>((*it));

    if(usingdec != NULL){

      for (auto iu = usingdec->shadow_begin(); iu != usingdec->shadow_end(); iu++){
        cxxRecordPtr = dyn_cast<clang::CXXRecordDecl>((*iu)->getTargetDecl());
        
        if(cxxRecordPtr != NULL){
          break;
        }
      }
    }
  }

  if(cxxRecordPtr == NULL){
    assert(false && "object is unexpected type");
    return NULL;
  }

  if(cxxRecordPtr->getDefinition() == NULL){
    assert(false && "using a class thats not fully declared");
    return NULL;
  }

  cxxRecordPtr = cxxRecordPtr->getDefinition();

  for(auto it = cxxRecordPtr->field_begin(); it != cxxRecordPtr->field_end(); it++){
    
    auto kindName = (*it)->getDeclKindName();
    auto name = (*it)->getName();
   
    if((*it)->getName() == fieldName){
      auto& qualType = (*it)->getType().split();

      //auto name2 = clang::QualType::getAsString(qualType);
      //string fieldType = qualType.Ty->getTypeClassName();

      return (*it);
    }
  }

  return NULL;
}

void RecordEntry::BuildFieldGetSet(const StringRef& objectType, const StringRef& fieldType, const StringRef& fieldOffset){
  
  assert(Type == Recorder_GetField || Type == Recorder_SetField);

  RecordOptions = (objectType+"| (FieldTypeLookup<"+fieldType+">::fieldtype << 8)|("+ fieldOffset+" << 16)").str();
  TraceRecorder = Type == Recorder_GetField ? "recff_GetObjectField " : "recff_SetObjectField";
}

RecorderCollection::RecorderCollection(bool verbose) : 
  SM(NULL), InModule(false), UnboundRecorder(){
   FunctionId = 0;
   Verbose = verbose;
}

RecorderCollection::~RecorderCollection(){

}

void RecorderCollection::SetCompilerInstance(clang::CompilerInstance& ci){
 
  SM = &ci.getSourceManager();
  CI = &ci;

  InModule = false;
  PrintPolicy = new clang::PrintingPolicy(ci.getLangOpts());
  //functionEntry = new RecordEntry();
}

typedef llvm::SmallVector<clang::DeclContext*, 4> DeclContextList;

using clang::DeclContext;
using clang::NestedNameSpecifier;

DeclContextList BuildContextChain(DeclContext *Start) {
  assert(Start && "Bulding a context chain from a null context");
  DeclContextList Chain;
  for (DeclContext *DC = Start->getPrimaryContext(); DC != NULL;
       DC = DC->getLookupParent()) {
    clang::NamespaceDecl *ND = dyn_cast<clang::NamespaceDecl>(DC);
    if (!DC->isInlineNamespace() && !DC->isTransparentContext() &&
        !(ND && ND->isAnonymousNamespace()))
      Chain.push_back(DC->getPrimaryContext());
  }
  return Chain;
}

static NestedNameSpecifier *getRequiredQualification(clang::ASTContext &Context, const DeclContext *CurContext, const clang::Type* type) {
  llvm::SmallVector<const DeclContext *, 4> TargetParents;
  
  const DeclContext *TargetContext = NULL;

  if(type->getAs<clang::TypedefType>()){
    auto typeDef = type->getAs<clang::TypedefType>();

    if(typeDef != NULL){
      TargetContext = typeDef->getDecl()->getDeclContext();
    }
  }

  if(TargetContext == NULL){
    return NULL;
  }

  for (const DeclContext *CommonAncestor = TargetContext;
       CommonAncestor && !CommonAncestor->Encloses(CurContext);
       CommonAncestor = CommonAncestor->getLookupParent()) {
    if (CommonAncestor->isTransparentContext() ||
        CommonAncestor->isFunctionOrMethod())
      continue;
    
    TargetParents.push_back(CommonAncestor);
  }
  
  NestedNameSpecifier *Result = 0;
  while (!TargetParents.empty()) {
    const DeclContext *Parent = TargetParents.back();
    TargetParents.pop_back();
    
    if (const clang::NamespaceDecl *Namespace = dyn_cast<clang::NamespaceDecl>(Parent)) {
      if (!Namespace->getIdentifier())
        continue;

      Result = NestedNameSpecifier::Create(Context, Result, Namespace);
    }
    else if (const clang::TagDecl *TD = dyn_cast<clang::TagDecl>(Parent))
      Result = NestedNameSpecifier::Create(Context, Result,
                                           false,
                                     Context.getTypeDeclType(TD).getTypePtr());
  }  
  return Result;
}

void RecorderCollection::LuaCFunctionDefined(const clang::FunctionDecl *func){

  if(UnboundRecorder == NULL){
    return;
  }
  
  int functionDefLineNumber = SM->getExpansionLineNumber(func->getLocStart());
  
  //the recorder definition has tobe on the same line as the line function is defined
  if(functionDefLineNumber != UnboundRecorder->RecordLineNumber){
    std::cerr << "Error function " << func->getName().str() << "was not on the same line number as the last Recorder definition at line " << UnboundRecorder->RecordLineNumber;
    UnboundRecorder.reset();
    return;
  }

  auto recorder = UnboundRecorder.release();
  

  //set the function name that the recorder is bound to
  recorder->SetFunctionName(func->getName());
  
  if((recorder->Type == Recorder_GetField || recorder->Type == Recorder_SetField) && recorder->RecordOptions != "0"){
      
    auto fieldInfo = GetFieldInfo(CI->getSema(), func->getDeclContext(), recorder->GetObjectName(), recorder->RecordOptions);
    
    if(fieldInfo == NULL){
      std::cerr << "Error failed to get field info for function " << func->getName().str() << "\n";
      recorder->Valid = false;
      recorder = NULL;
      return;
    }

    clang::QualType fieldType = fieldInfo->getType();
    std::string fieldTypeString;
/*
    fieldType->dump();

    clang::QualType conType = fieldType.getCanonicalType();
    conType->dump();
*/

    auto scope = getRequiredQualification(CI->getASTContext(), func->getDeclContext(),  fieldType.getTypePtr());

    if(scope != NULL){

      llvm::raw_string_ostream output(fieldTypeString);

      scope->print(output, *PrintPolicy);
      output << fieldType.getBaseTypeIdentifier()->getName();

      output.flush();
    }else{
      
      fieldTypeString = clang::QualType::getAsString(fieldType.split());
    } 
    
    auto name12 = fieldInfo->getQualifiedNameAsString();

    recorder->BuildFieldGetSet(recorder->GetObjectName(), fieldTypeString, "offsetof("+recorder->GetObjectName()+", "+ recorder->RecordOptions+ ")");
  }

  RegisterEntryToGroup(recorder);

  FunctionId++;
}

 void RecorderCollection::RecorderFinalized(RecordEntry* recorder){

   AllFunctions.push_back(recorder);
  
  if(!recorder->Valid){
    //TODO some verbose output
    return;
  }

  if(UnboundRecorder != NULL){
    //TODO 
    std::cerr << "Error no function was found for the previous recorder definition at line " << UnboundRecorder->RecorderLine;
    return;
  }

  //frees previous UnboundRecorder if there was no function definition was found to attach to it
  UnboundRecorder.reset(recorder);

  //we only increment the functionId when we find a valid function definition to bind the recorder to
  UnboundRecorder->FunctionId = FunctionId;

  //if(InModule){
  //  if(!functionEntry->Name.empty()){
  //    RegisterEntryToGroup(functionEntry);
  //  }
  //
  //}else{
  //  GobalFunctions.push_back(functionEntry);
  //}
}
 
using clang::DiagnosticsEngine;

void RecorderCollection::RegisterEntryToGroup(RecordEntry* entry){


  auto Objectname = entry->GetObjectName();

  if(Objectname.empty()){
    auto& diag = CI->getDiagnostics();

    unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Error, "Record entry %s is missing a under slash in its name.");
    clang::DiagnosticBuilder B = CI->getDiagnostics().Report(id);
    B.AddString(entry->Name);

    return;
  }

  auto funcList = GetFunctionList(Objectname);

  if(entry->Name.find("___") != string::npos){
    funcList->AddMetaFunction(entry);
  }else{
    funcList->AddMemberFunction(entry);
  }

}

ObjectRecorderData* RecorderCollection::GetFunctionList(std::string& objectName){

  auto result = ObjectFunctions.find(objectName);

  if(result != ObjectFunctions.end()){
    return result->second;
  }

  auto returnValue = new ObjectRecorderData(objectName);

  ObjectFunctions[objectName] = returnValue;

  return returnValue;
}

void RecorderCollection::ReportError(const char* fmtmsg, const StringRef fmtarg){

  auto& diag = CI->getDiagnostics();
  unsigned id = diag.getDiagnosticIDs()->getCustomDiagID((clang::DiagnosticIDs::Level)DiagnosticsEngine::Error, fmtmsg);

  clang::DiagnosticBuilder B = CI->getDiagnostics().Report(id);
  B.AddString(fmtarg);
}

void RecorderCollection::ModuleDefined(std::string& name, std::string& moduleType){
  
  auto object = GetFunctionList(name);

  if(moduleType == "userdata"){
    object->ObjectType = Object_Userdata;
  }else if(moduleType == "cdata"){
    object->ObjectType = Object_CData;
  }else{
    ReportError("Module %s specified invalid", name);
  }
}