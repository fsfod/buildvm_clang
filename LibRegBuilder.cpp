#include "LibRegBuilder.h"


using std::string;

LibRegBuilder::LibRegBuilder(MacroCollector* collectedMacros, const std::string& outputPath) : CollectedMacros(collectedMacros), 
  CurrentObject(NULL), output(outputPath){
}


void LibRegBuilder::WriteRecorderArray(std::vector<RecordEntry*>& functionList){
  //{&TraceRecorder, RecordData, }


  output << "FunctionInfo RecorderInfo[] = {\n";

  for each (RecordEntry* entry in functionList){
    output << "  {&" << entry->TraceRecorder << ", "<< entry->RecordOptions <<", \"" << entry->Name << "\", &lj_cf_" << entry->Name << "},\n";
  }

  output << "};\n\n";
}

void LibRegBuilder::WriteExtenList(std::vector<RecordEntry*>& functionList){

  output << "struct RecordFFData;\n";

  for each (RecordEntry* var in functionList){
    output << "extern int lj_cf_" << var->Name << "(lua_State* L);\n";
    output << "extern void LJ_FASTCALL " << var->TraceRecorder <<"(jit_State *J, struct RecordFFData *rd);\n";
  }

  output << "\n\n";
}

void LibRegBuilder::WriteRegObjectFunctionStart(const string& objectName){

  output << "void Register_" << objectName << "(lua_State* L, GCtab* mtList, GCtab* membersList){\n\n";
  output << "  GCfunc* func;\n";
  output << "  TValue* slot;\n";
}

void LibRegBuilder::WriteFunctionInit(RecordEntry& entry, const char* outputTable, int subNameStart){

  output << "\n  func = lj_func_newfastC(L, &RecorderInfo[" << entry.FunctionId << "], " << entry.PushStack.size() << ");\n";

  for(int i = 0; i < entry.PushStack.size() ;i++){
    const PushEntry& pushValue = entry.PushStack[i];

    switch (pushValue.Type){
      case PushType_String:
        output << "  setstrV(L, &func->c.upvalue[" << i << "], lj_str_newz(L, \"" << (*pushValue.StringLiteral) << "\"));\n";
      break;

      case PushType_MemberTable:
        assert(CurrentObject->MemberFunctions.size() != 0);
        output << "  settabV(L, &func->c.upvalue[" << i << "], memberTable);\n";
      break;

      case PushType_StackSlotBase:
        output << "  copyTV(L, &func->c.upvalue[" << i << "], L->base+" << pushValue.StackSlot << ");\n";
      break;

      case PushType_StackSlot:
        output << "  copyTV(L, &func->c.upvalue[" << i << "], L->top-" << pushValue.StackSlot << ");\n";
      break;

      default:
        assert(false, "unknown push type");
      break;
    }
  }

  string& name = entry.Name.find('_') != string::npos ?  entry.Name.substr(subNameStart) : entry.Name;

  output << "  slot = lj_tab_setstr(L, " << outputTable << ", lj_str_newz(L, \"" << name << "\"));\n";
  output << "  setfuncV(L, slot, func);\n";
  output << "  L->top--;\n";
}

const char* HeaderList = "extern \"C\"{\n"
"#include \"lj_obj.h\"\n"
"#include \"lj_str.h\"\n"
"#include \"lj_tab.h\"\n"
"#include \"lj_func.h\"\n"
"#include \"lj_jit.h\"\n"
"#include \"lj_ir.h\"\n"
"}\n"


void LibRegBuilder::WriteTableCreate(const std::string& tableName, int size, const std::string& destTable, const std::string& destKey){

  output << "  GCtab *" << tableName << " = lj_tab_new(L, 0, hsize2hbits(" << size << "));\n";
  output << "  settabV(L, L->top++, " << tableName << ");\n";

  output << "  slot = lj_tab_setstr(L, " << destTable << ", lj_str_newz(L, \"" << destKey << "\"));\n";
  output << "  settabV(L, slot, " << tableName << ");\n";
  output << "  L->top--;\n";
}

void LibRegBuilder::WriteLibReg(){

  output << HeaderList;

  WriteExtenList(CollectedMacros->AllFunctions);

  WriteRecorderArray(CollectedMacros->AllFunctions);

  auto start = CollectedMacros->ObjectFunctions.begin();
  auto end = CollectedMacros->ObjectFunctions.end();

  if(CollectedMacros->GobalFunctions.size() != 0){
    
    output << "void Register_Lib(lua_State* L, GCtab* libTable){\n\n";
    output << "  GCfunc* func;\n";
    output << "  TValue* slot;\n";

    for each (RecordEntry* var in CollectedMacros->GobalFunctions){
      WriteFunctionInit(*var, "libTable", 0);
    }

    output << "}\n\n";
  }

  for(auto objectEntry = start; objectEntry != end ;objectEntry++){

    CurrentObject = objectEntry->second;

    WriteRegObjectFunctionStart(objectEntry->first);

    auto& memberList = objectEntry->second->MemberFunctions;
    auto& metaList = objectEntry->second->MetaFunctions;
    
    if(memberList.size() != 0){
      WriteTableCreate("memberTable", memberList.size(), "membersList", objectEntry->first);
    }
    
    if(metaList.size() != 0){
       WriteTableCreate("metaTable", metaList.size(), "mtList", objectEntry->first);
      //output << "  GCtab *metaTable = lj_tab_new(L, 0, hsize2hbits(" << metaList.size() << "));\n";
      //output << "  settabV(L, L->top++, metaTable);\n";
      //
      //output << "  slot = lj_tab_setstr(L, mtList, lj_str_newz(L, \"" << objectEntry->first << "\"));\n";
      //output << "  settabV(L, slot, metaTable);\n";
      //output << "  L->top--;\n";
    }

    if(memberList.size() != 0)output << "  settabV(L, L->top++, memberTable);\n";

 
    if(memberList.size() != 0){
      for each (RecordEntry* var in memberList){
        WriteFunctionInit(*var, "memberTable", objectEntry->first.size()+1);
      }
    }

    if(metaList.size() != 0){
      for each (RecordEntry* var in metaList){
        WriteFunctionInit(*var, "metaTable", objectEntry->first.size()+1);
      }
    }

    output << "}\n\n";
  }

  output.flush();
}