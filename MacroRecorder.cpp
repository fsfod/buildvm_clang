
#include "MacroRecorder.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/Sema.h"
#include <iostream>

using std::string;
using namespace clang;

const char LJLib[] = "LJLIB";
int LJLib_int = *(int*)(&LJLib);

string pushMacro = "LJLIB_PUSH";
string recordMacro = "LJLIB_REC";
string CFuncMacro = "LJLIB_CF";
string ModuleStartMacro  = "LJLIB_MODULE_";

MacroCollector::MacroCollector() : SM(NULL), InModule(false){
   functionEntry = new RecordEntry();
   FunctionId = 0;
}

MacroCollector::~MacroCollector(){
  assert(false);
}


void MacroCollector::SetCompilerInstance(clang::CompilerInstance& ci){
 
  SM = &ci.getSourceManager();
  
  InModule = false;
  functionEntry = new RecordEntry();
}

std::string MacroRecorder::GetMacroArgs(const Token &MacroNameTok, SourceRange& Range){

  int nameLength = MacroNameTok.getIdentifierInfo()->getLength();
  int start = SM->getFileOffset(Range.getBegin())+nameLength+1;
  int end = SM->getFileOffset(Range.getEnd());

  const char* macroStart = SM->getCharacterData(Range.getBegin())+nameLength+1;

  return string(macroStart, end-start);
}

void MacroRecorder::MacroDefined(const clang::Token &macroNameTok, const clang::MacroInfo* MI){
  
  StringRef name = macroNameTok.getIdentifierInfo()->getName();

  if(name.size() < 11 || *((int*)name.data()) != LJLib_int)return;

  if(name.startswith(ModuleStartMacro)){
    Collector->SetInModule(name.substr(ModuleStartMacro.size()));
  }
}

void MacroRecorder::MacroExpands(const Token &macroNameTok, const clang::MacroInfo* MI, SourceRange range) {

  std::string name = macroNameTok.getIdentifierInfo()->getName();
  std::string macroArgs;

  if(name.size() < 6 || *((int*)name.c_str()) != LJLib_int)return;
  
  bool isPush = name == pushMacro;
  bool isRecord = !isPush && name == recordMacro;

  macroArgs = GetMacroArgs(macroNameTok, range);

  clang::Lexer lexer(range.getBegin().getLocWithOffset(name.size()+1),
                     CI->getLangOpts(),
                     macroArgs.c_str(), macroArgs.c_str(), macroArgs.c_str()+macroArgs.size());
  
  if(name == "LJLIB_ALIAS"){
    Collector->ProcessStackAlias(lexer);
  }else if(isPush){
    Collector->ProcessPush(lexer, macroNameTok, range);
    std::cout << "Push " << macroArgs  << "\n";
  }else if(isRecord){
    RecordEntry* recordedFunction  = Collector->functionEntry;
    Collector->ProcessRecord(lexer, macroNameTok, range);

    std::cout << "Record(" << recordedFunction->Name << "):" << recordedFunction->RecordLineNumber << "  " << macroArgs  << "\n\n";
  }else if(name == CFuncMacro){
    Collector->functionEntry->Name = macroArgs;
  }
}

StringRef MacroCollector::TokenToStringRef(Token& tok){
   
  switch(tok.getKind()){
    case tok::raw_identifier:
      return StringRef(tok.getRawIdentifierData(), tok.getLength());

    case tok::numeric_constant:
      return StringRef(tok.getLiteralData(), tok.getLength());
    
    case tok::string_literal:
      return StringRef(tok.getLiteralData()+1, tok.getLength()-2);

    default:
      return StringRef();
  }
}

ObjectData* MacroCollector::GetFunctionList(std::string& objectName){

  auto result = ObjectFunctions.find(objectName);

  if(result != ObjectFunctions.end()){
    return result->second;
  }

  ObjectData* returnValue = new ObjectData();

  ObjectFunctions[objectName] = returnValue;

  return returnValue;
}
  
PushEntry MacroCollector::ParsePushValue(clang::Lexer& lexer){
  Token tok;
  lexer.LexFromRawLexer(tok);
  
  if(tok.is(tok::string_literal)){
   return PushEntry(TokenToStringRef(tok));
  }else if(tok.is(tok::raw_identifier)){
    StringRef identifer = TokenToStringRef(tok);

    bool isTop = identifer == "top";
    bool isBase = !isTop && identifer == "base";

    if(isTop || isBase){
      assert(!lexer.LexFromRawLexer(tok) && tok.is(isTop ? tok::minus : tok::plus));
      assert(lexer.LexFromRawLexer(tok) && tok.is(tok::numeric_constant));

      int offset;
      assert(!TokenToStringRef(tok).getAsInteger(10, offset));

      return PushEntry(isTop ? PushType_StackSlot : PushType_StackSlotBase, offset);

    }else if(identifer == "MemberList"){
      return PushEntry(PushType_MemberTable);
    }else{

      auto alias = StackAlias.find(identifer);

      if(alias == StackAlias.end()){
        assert(false && "unhandled push type with identifer");
      }

      return alias->second;
    }
  }else{
    assert(false && "unhandled push type");
  }
}

void MacroCollector::ProcessStackAlias(Lexer& lexer){
  Token tok;
  assert(!lexer.LexFromRawLexer(tok) && tok.is(tok::raw_identifier));

  StringRef aliasName = TokenToStringRef(tok);

  assert(!lexer.LexFromRawLexer(tok) && tok.is(tok::comma));

  StackAlias[aliasName] = ParsePushValue(lexer);
}

void MacroCollector::ProcessPush(clang::Lexer& lexer, const Token &MacroNameTok, SourceRange range){
  functionEntry->PushStack.push_back(ParsePushValue(lexer));
}

void MacroCollector::ProcessRecord(clang::Lexer& lexer, const Token &MacroNameTok, SourceRange range){
  
  auto decompLoc = SM->getDecomposedExpansionLoc(range.getBegin());

  functionEntry->RecordLineNumber = SM->getLineNumber(decompLoc.first, decompLoc.second);

  Token tok;
  lexer.LexFromRawLexer(tok);

  if(tok.is(tok::period)){
    functionEntry->RecordOptions = "0";
    functionEntry->TraceRecorder = "recff_"+functionEntry->Name;

  }else if(tok.is(tok::raw_identifier)){
    functionEntry->TraceRecorder = "recff_"+TokenToStringRef(tok).str();

    lexer.LexFromRawLexer(tok);

    if(tok.is(tok::eof)){
      functionEntry->RecordOptions = "0";
    }else if(tok.is(tok::raw_identifier)){
      functionEntry->RecordOptions = tok.getRawIdentifierData();
    }else if(tok.is(tok::numeric_constant)){
      functionEntry->RecordOptions = TokenToStringRef(tok);
    }else{
      assert(false && "unhandled record data format");
    }
  }else{
    assert(false && "unreconized record info");
  }

  if(InModule){

    int underSlash = functionEntry->Name.find('_');

    auto funcList = GetFunctionList(functionEntry->Name.substr(0, underSlash));

    if(functionEntry->Name.find("___") != string::npos){
      funcList->MetaFunctions.push_back(functionEntry);
    }else{
      funcList->MemberFunctions.push_back(functionEntry);
    }
  }else{
    GobalFunctions.push_back(functionEntry);
  }

  functionEntry->FunctionId = FunctionId++;

  AllFunctions.push_back(functionEntry);

  functionEntry = new RecordEntry();
}