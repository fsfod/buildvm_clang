#include "MacroRecorder.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Lex/MacroArgs.h"

#include <iostream>
#include <sstream> 

using std::string;
using std::pair;
using namespace clang;

const char LJLib[] = "LJFF_";
int LJLib_int = *reinterpret_cast<const int*>(&LJLib);


class ParseException : std::exception{

public:
  ParseException(const string& parseError, SourceLocation location) :
    Location(location), std::exception(parseError.c_str()){
  }

  const SourceLocation Location;
};

MacroRecorder::MacroRecorder(clang::CompilerInstance& ci, RecorderCollection* collector, bool verbose) : 
      CI(&ci), SM(&ci.getSourceManager()), Collector(collector), Verbose(verbose) {
   
  collector->SetCompilerInstance(ci);
  functionEntry = new RecordEntry();
  SetupKeywords();
}

string MacroRecorder::GetMacroArgs(const clang::Token &macroNameTok, SourceRange& Range){

  int nameLength = macroNameTok.getIdentifierInfo()->getLength();
  int start = SM->getFileOffset(Range.getBegin())+nameLength+1;
  int end = SM->getFileOffset(Range.getEnd());

  const char* macroStart = SM->getCharacterData(Range.getBegin())+nameLength+1;

  return string(macroStart, end-start);
}

llvm::StringMap<int> MacroRecorder::KeywordLookup;

#define LJ_KEYWORDS(_) \
  _(ALIAS,        Parse_StackAlias) \
  _(MODULE,       Parse_Module) \
  _(NEEDSFLAG,    Parse_NeedsFlag) \
  _(NOEXTERN,     Parse_NoExtern) \
  _(PUSH,         Parse_Push) \
  _(REC,          Parse_Record) \
  _(REC_GETFIELD, Parse_Record_GetSetField) \
  _(REC_SETFIELD, Parse_Record_GetSetField) \

#define KEYWORD_ENUM(keyword, handler) KW_##keyword,

enum KeywordIds{
 KW_Invalid = 0,
 LJ_KEYWORDS(KEYWORD_ENUM)
};

#define KEYWORD_Lookup(keyword, handler) KeywordLookup[#keyword] = KW_##keyword;

void MacroRecorder::SetupKeywords(){
  
  if(!KeywordLookup.empty()){
    return;
  }

  LJ_KEYWORDS(KEYWORD_Lookup);
}

#define KEYWORD_SWITCH(keyword, handler) case KW_##keyword: \
  handler(); \
  break; \

void MacroRecorder::MacroExpands(const clang::Token &macroNameTok, const clang::MacroDefinition &MD, SourceRange range, const clang::MacroArgs *Args) {

  StringRef name = macroNameTok.getIdentifierInfo()->getName();

  if(name.size() < 5 || *((int*)name.data()) != LJLib_int){
    return;
  }
  
  MacroLocation = range;
  EndOfMacro = false;

  MacroArgs = GetMacroArgs(macroNameTok, range);

// Args->getUnexpArgument();

   // Args->

  clang::Lexer lexer(range.getBegin().getLocWithOffset(name.size()+1),
                     CI->getLangOpts(),
                     MacroArgs.c_str(), MacroArgs.c_str(), MacroArgs.c_str()+MacroArgs.size());
  
  MacroLexer = &lexer;

  int lineNumber = GetStartingLineNumber();

  CurrentKeyword = name.substr(strlen("LJFF_"));

  int keyword = KeywordLookup[CurrentKeyword];

  switch (keyword){
    LJ_KEYWORDS(KEYWORD_SWITCH)

    default:
      assert(false && "Unknown keyword");
      break;
  }
}

void MacroRecorder::Parse_Module(){

  if(!LexExpect(tok::raw_identifier)){
    SetCurrentEntryInvalid("expected name of module");
    return;
  }

  string name = TokenToStringRef(tok);

  if(!LexExpect(tok::comma, tok::raw_identifier)){
    SetCurrentEntryInvalid("expected module type");
    return;
  }

  string moduleType = TokenToStringRef(tok);


  Collector->ModuleDefined(name, moduleType);
}

void MacroRecorder::Parse_NeedsFlag(){

  if(!LexExpectEnd(tok::raw_identifier)){
    SetCurrentEntryInvalid("expected a name of an enum entry");
    return;
  }

  functionEntry->RequiredFlag = TokenToStringRef(tok).str();
}

void MacroRecorder::Parse_NoExtern(){

  if(!LexExpectEnd(tok::raw_identifier)){
    SetCurrentEntryInvalid("expected a name of a function for NOEXTERN");
    return;
  }

  NoExtern.insert(TokenToStringRef(tok));
}

StringRef MacroRecorder::TokenToStringRef(Token& tok){
   
  switch(tok.getKind()){
    case tok::raw_identifier:
      return tok.getRawIdentifier();

    case tok::numeric_constant:
      return StringRef(tok.getLiteralData(), tok.getLength());
    
    case tok::string_literal:
      return StringRef(tok.getLiteralData()+1, tok.getLength()-2);

    default:
      return StringRef();
  }
}

/*
  used to add upvalues to the Lua function

  Supported Push types
    1. LJLIB_PUSH("x") quoted strings e.g. that are turned into Lua strings;
    2. LJLIB_PUSH(MemberList) The Lua table containing the members of the current type Usefule for __index meta-methods
    3. LJLIB_PUSH(base+3)
*/
bool MacroRecorder::ParsePushValue(PushEntry& result){

  MacroLexer->LexFromRawLexer(tok);
  
  if(tok.is(tok::string_literal)){
   result = PushEntry(TokenToStringRef(tok));
  }else if(tok.is(tok::raw_identifier)){
    StringRef identifer = TokenToStringRef(tok);

    bool isTop = identifer == "top";
    bool isBase = !isTop && identifer == "base";

    if(isTop || isBase){
      if(!LexExpect(isTop ? tok::minus : tok::plus)){
        SetCurrentEntryInvalid("expected + for a stack base offset or a - for stack top offset");
       return false;
      }

      if(!LexExpectEnd(tok::numeric_constant)){
        SetCurrentEntryInvalid("expected number for a stack offset");
       return false;
      }

      int offset;
      assert(!TokenToStringRef(tok).getAsInteger(10, offset));

      result = PushEntry(isTop ? PushType_StackSlot : PushType_StackSlotBase, offset);

    }else if(identifer == "MemberList"){
      result = PushEntry(PushType_MemberTable);
    }else if(identifer == "StaticMemberList"){
      result = PushEntry(PushType_StaticMemberTable);

    }else if(identifer == "MT" || identifer == "Global"){
      bool mt = identifer == "MT";
      result = PushEntry(mt ? PushType_MT : PushType_Global);
           
      if(!LexExpect(tok::comma) || !LexExpectEnd(tok::raw_identifier)){
        SetCurrentEntryInvalid("Expected %s table name");
       return false;
      }

      result.StringLiteral = new std::string(TokenToStringRef(tok));
    }else{

      auto alias = StackAlias.find(identifer);

      if(alias == StackAlias.end()){
        SetCurrentEntryInvalid("Unknown stack alias %s");
       return false;
      }

      result = alias->second;
    }
  }else{
    SetCurrentEntryInvalid("Unknown push type ");
   return false;
  }

  return true;
}

void MacroRecorder::Parse_StackAlias(){

  if(Verbose){
    std::cout << GetStartingLineNumber() << ": StackAlias " << MacroArgs  << "\n";
  }

  if(!LexExpect(tok::raw_identifier)){
    SetCurrentEntryInvalid("Expected name for stack alias");
   return;
  }

  StringRef aliasName = TokenToStringRef(tok);

  if(!LexExpect(tok::comma)){
    SetCurrentEntryInvalid("Expected number for the second arg of stack alias");
   return;
  }

  PushEntry entry;

  if(ParsePushValue(entry)){
    StackAlias[aliasName] = entry;
  }

}

void MacroRecorder::Parse_Push(){
  PushEntry entry;

  if(Verbose){
    std::cout << GetStartingLineNumber() << ": Push " << MacroArgs  << "\n";
  }

  if(ParsePushValue(entry)){
    functionEntry->PushStack.push_back(entry);
    
    if(entry.Type == PushType_MemberTable || entry.Type == PushType_StaticMemberTable){
      functionEntry->NeedsMembersTable = true;
    }
  }
}

void MacroRecorder::Parse_Record_GetSetField(){
  
  if(Verbose){
    std::cout << GetStartingLineNumber() << ": Record(" << CurrentKeyword.str() << "):" << "  " << MacroArgs  << "\n\n";
  }

  bool isSet = CurrentKeyword == "REC_SETFIELD";

  functionEntry->RecordLineNumber = GetStartingLineNumber();
  functionEntry->Type = isSet ? Recorder_SetField: Recorder_GetField;
  
  if(!LexExpect(tok::raw_identifier)){
    SetCurrentEntryInvalid("Expected object type name or field name for SetField/ReturnField definition");
   return;
  }
  
  auto arg1 = TokenToStringRef(tok).str();

  //We only have a single arg which must be the field name the object type is implicitly defined through the function
  if(EndOfMacro){
    functionEntry->RecordOptions = arg1;
    functionEntry->TraceRecorder = "";
    
    FinalizeRecorder();
   return;
  }

  functionEntry->TraceRecorder = (isSet ? "SetObjectField" : "GetObjectField") +TokenToStringRef(tok).str()+", ";

  if(!LexExpect(tok::comma) || !LexExpect(tok::raw_identifier)){
    SetCurrentEntryInvalid("Invalid or missing field type for SetField/ReturnField definition");
   return;
  }

  //field type
  auto templateDef = TokenToStringRef(tok).str()+"|(";

  if(!LexExpect(tok::comma) || !MacroLexer->LexFromRawLexer(tok)){
    SetCurrentEntryInvalid("Invalid or missing field offset for SetField/ReturnField definition");
   return;
  }
  
  //field offset
  if(tok.is(tok::raw_identifier) || tok.is(tok::numeric_constant)){
    templateDef += TokenToStringRef(tok).str();
  }else{
    SetCurrentEntryInvalid("unknown field offset value type for SetField/ReturnField definition");
   return;
  }

  templateDef = templateDef+" << 16)";

  functionEntry->RecordOptions = templateDef;

  FinalizeRecorder();


}

bool MacroRecorder::SkipToNextToken(clang::tok::TokenKind endToken){

  int ParenCount = 0, BraceCount = 0, BracketCount = 0;

  SourceLocation loc = MacroLexer->getSourceLocation();
  
  while(tok.isNot(tok::eof)){

    if(tok.is(endToken) && ParenCount == 0 && BraceCount == 0 && BracketCount == 0){
      //we've found the token and were not nested in anything
      break;
    }

    switch (tok.getKind()) {
      case tok::l_brace:
        BraceCount++;
      break;

      case tok::l_paren:
        ParenCount++;
      break;

      case tok::l_square:
        BracketCount++;
      break;
      
      case tok::r_brace:
        if(--BraceCount < 0){
          SetCurrentEntryInvalid("too many closing braces in parameter");
          return false;
        }
      break;

      case tok::r_paren:
        if(--ParenCount < 0){
          SetCurrentEntryInvalid("too many closing braces in parameter");
          return false;
        }
      break;

      case tok::r_square:
        if(--BracketCount < 0){
          SetCurrentEntryInvalid("too many closing brackets in parameter");
          return false;
        }
      break;

      default:
      break;
    }

    MacroLexer->LexFromRawLexer(tok);
  }

  if(ParenCount == 0 && BraceCount == 0 && BracketCount == 0){
    return true;
  }else{
    SetCurrentEntryInvalid("Missing closing brace/bracket/parenin in parameter");
    return false;
  }
}

bool MacroRecorder::ParseRecordArgParam(std::string& result){

  if(tok.is(tok::comma)){
    MacroLexer->LexFromRawLexer(tok);
  }

  const char* paramStart = SM->getCharacterData(tok.getLocation());


  if(!SkipToNextToken(tok::comma)){
    //there were too many open or closing braces
    return false;
  }

  const char* paramEnd = SM->getCharacterData(tok.getLocation());

  result = string(paramStart, paramEnd);

  return true;
}

//valid forms of recorder specifiers 
//LJLIB_REC(.) the name of the tracerecorder id implicitly generated as recff_ + the name of the function this record specifier  
//LJLIB_REC(SomeTraceRecorder, [,0-4 option values must fit in a 32 bit int for 2 options values they will be treated as 16 bit its and 
// or'ed together first arg will be placed in the lower 16 bits  of the record options int])
//LJLIB_REC(&ReturnField<Model, Vec3, ModelOffset_BoundingBox>)
//
//the option expressions are also wrapped in brackets so you can use complex expressions instead of just numbers
void MacroRecorder::Parse_Record(){
  
  functionEntry->RecordLineNumber = GetStartingLineNumber();

  MacroLexer->LexFromRawLexer(tok);

  if(tok.is(tok::period)){
    //a dot means we automatically calculate the name as  recff_ + the name of the function
    functionEntry->RecordOptions = "0";
    
    if(!functionEntry->Name.empty()){
      functionEntry->TraceRecorder = "recff_"+functionEntry->Name;
    }else{
      //we have no name for the function so set the name as a dot tobe fixed up in MyASTConsumer::MatchMacrosToFunction
      functionEntry->TraceRecorder  = ".";
    }

  }else if(tok.is(tok::raw_identifier)){
    //a named recorder was specified 
    functionEntry->TraceRecorder = "recff_"+TokenToStringRef(tok).str();

    MacroLexer->LexFromRawLexer(tok);

    if(tok.is(tok::eof)){
      functionEntry->RecordOptions = "0";
    }else{
      //record options were specified so try to parse them
      string optionParam;
      
      if(!ParseRecordArgParam(optionParam)){
        return;
      }

      if(tok.is(tok::eof)){
        //just a single recorder option was specified
        functionEntry->RecordOptions = optionParam;
      }else{
        //more than one record option parse upto 3 more
        std::vector<string> options;
        options.push_back(optionParam);

        while(tok.isNot(tok::eof)){
          if(!ParseRecordArgParam(optionParam))return;
          options.push_back(optionParam);
        }

        if(options.size() > 4){
            SetCurrentEntryInvalid("Too many options values for recorder max is 4");
          return;
        }

        auto it = options.begin();

        std::stringstream buff;

        if(options.size() == 2){
          //pack 2 options both need tobe smaller than MAX uint16_t
          buff << "((" << options[1] << ") << 16)|";
        }else{

          for(int i = options.size()-2; i != 0 ;--i){
            buff << '((' << options[i] << ") <<" << (i*4) << ')|';
          }
        }

        //write the first option in the lower bits is either 
        buff << '(' << options[0] << ')';

        functionEntry->RecordOptions = buff.str();
      }
    }
  }else if(tok.is(tok::amp)){
    //a c++ template based recorder was specified in the form of an & address of operator followed by the template instigation
    //LJLIB_REC(&ReturnField<Model, Vec3, ModelOffset_BoundingBox>)
    MacroLexer->LexFromRawLexer(tok);
    const char* loc = MacroLexer->getBufferLocation();

    if(tok.isNot(tok::raw_identifier)){
      SetCurrentEntryInvalid("Expected a template function name");
      return;
    }

    MacroLexer->LexFromRawLexer(tok);
    const char* name = tok.getName();

    MacroLexer->LexFromRawLexer(tok);

  }else{
    const char* name = tok.getName();

    SetCurrentEntryInvalid("Unrecognized record info");
    return;
  }

  if(NoExtern.find(functionEntry->TraceRecorder) != NoExtern.end()){
    functionEntry->NoRecorderExtern = true;
  }

  FinalizeRecorder();
}

void MacroRecorder::FinalizeRecorder(){
  Collector->RecorderFinalized(functionEntry);
  functionEntry = new RecordEntry();
}

void MacroRecorder::SetCurrentEntryInvalid(const char* reason, StringRef fmtarg){

  DiagnosticsEngine& diag = CI->getDiagnostics();
  unsigned id = diag.getDiagnosticIDs()->getCustomDiagID((clang::DiagnosticIDs::Level)DiagnosticsEngine::Error, reason);

  DiagnosticBuilder B = CI->getDiagnostics().Report(tok.getLastLoc(), id);
  B.AddString(fmtarg);

  functionEntry->Valid = false;
}


