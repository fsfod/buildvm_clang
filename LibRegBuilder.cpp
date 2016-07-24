#include "LibRegBuilder.h"


using std::string;

LibRegBuilder::LibRegBuilder(RecorderCollection* collectedMacros, const std::string& outputPath) : CollectedMacros(collectedMacros), 
  CurrentObject(NULL), output(outputPath){
}


void LibRegBuilder::WriteRecorderArray(std::vector<RecordEntry*>& functionList){

  output << "RecorderInfo TraceRecorderInfo[] = {\n";

  for each (RecordEntry* entry in functionList){
    output << "  {&" << entry->TraceRecorder << ", "<< entry->RecordOptions <<", \"" << entry->Name << "\"},\n";
  }

  output << "};\n\n";
}

void LibRegBuilder::WriteExtenList(std::vector<RecordEntry*>& functionList){

  output << "struct RecordFFData;\n";
  output << "struct jit_State;\n\n";

  for each (RecordEntry* var in functionList){
    if(var->Name.empty())continue;
    output << "extern int " << var->Name << "(lua_State* L);\n";
  }

  output << "\n\n";
}

//write the header of the function that registers an objects member and meta functions table
void LibRegBuilder::WriteRegObjectFunctionStart(const string& objectName){

  output << "void Register_" << objectName << "(lua_State* L, uint32_t options){\n";
}

void LibRegBuilder::WriteFunctionInit(RecordEntry& entry, const char* outputTable, int subNameStart){
  
  if(!entry.Valid){
    return;
  }

  if(!entry.RequiredFlag.empty()){
    output << "\n  if((options&" << entry.RequiredFlag << ") != 0){\n";
  }else{
    output << "\n";
  }

  for(size_t i = 0; i < entry.PushStack.size() ;i++){
    const PushEntry& pushValue = entry.PushStack[i];

    switch (pushValue.Type){
      case PushType_String:
        output << "  lua_pushstring(L, \"" << (*pushValue.StringLiteral) << "\");\n";
      break;

      case PushType_MemberTable:
       // assert(CurrentObject->MemberFunctions.size() != 0);
        output << "  lua_pushvalue(L, memberTable);\n";
      break;

      case PushType_StaticMemberTable:
       // assert(CurrentObject->MemberFunctions.size() != 0);
        output << "  lua_getfield(L, LUA_GLOBALSINDEX, \"" << CurrentObject->Name;

        if(CurrentObject->ObjectType == Object_CData){
          output << "_FFIIndex\");\n";
        }else{
          output << "\");\n";
        }
      break;

      case PushType_MT:
        output << "  luaL_newmetatable(L, \"" << (*pushValue.StringLiteral) << "\");\n";
      break;

      case PushType_Global:
        output << "  lua_getfield(L, LUA_GLOBALSINDEX, \"" << (*pushValue.StringLiteral) << "\");\n";
      break;

      case PushType_StackSlotBase:
        output << "  lua_pushvalue(L, "<< pushValue.StackSlot << ");\n";
      break;

      case PushType_StackSlot:
        output << "  lua_pushvalue(L, -" << pushValue.StackSlot << ");\n";
      break;

      default:
        assert(false && "unknown push type");
      break;
    }
  }
  
  output << "  lua_pushcfastfunc(L, &" << entry.Name << ", " << entry.PushStack.size() << ",  &" << entry.TraceRecorder << ", "<< entry.RecordOptions <<", \"" << entry.Name << "\");\n";

  string& name = entry.Name.find('_') != string::npos ?  entry.Name.substr(subNameStart) : entry.Name;

  output << "  lua_setfield(L, " << outputTable << ", \"" << name << "\");\n";

  if(!entry.RequiredFlag.empty()){
    output << "  }\n";
  }
}


const char* HeaderList = "#define LUA_LIB\n\
extern \"C\"{\n\
#include \"lj_ir.h\"\n\
}\n\
\n\n";


void LibRegBuilder::WriteTableCreate(const std::string& tableName, int size, const std::string& destTable, const std::string& destKey, int arraySize){

  if(CurrentObject->ObjectType == Object_CData){
    
  }

  output << "\n  lua_createtable(L, "<< arraySize <<", " << size << ");\n";
  output << "  lua_pushvalue(L, -1);\n";
  output << "  lua_setfield(L, " << destTable <<  ", \"" << destKey << "\");\n";
  output << "  int " << tableName << " = lua_gettop(L);\n";
}

void LibRegBuilder::WriteCDataMtCreate(int size, const std::string& typeId){

  output << "\n  lua_createtable(L, 0, " << size << ");\n";
  output << "  SetCTypeMT(L, " << typeId << ", tabV(L->top-1));\n";
  
  output << "  int metaTable = lua_gettop(L);\n";
}

bool LibRegBuilder::RecordersValid(){

  for each (RecordEntry* var in CollectedMacros->AllFunctions){
    if(!var->Valid){
      return false;
    }
  }

  return true;
}

void LibRegBuilder::WriteLibReg(std::vector<string>& includeList){

  output << HeaderList;

  for(auto include = includeList.begin(); include != includeList.end() ;include++){
    output << "#include \"" << *include << "\"\n";
  }

  WriteExtenList(CollectedMacros->AllFunctions);

  //WriteRecorderArray(CollectedMacros->AllFunctions);

  auto start = CollectedMacros->ObjectFunctions.begin();
  auto end = CollectedMacros->ObjectFunctions.end();

  for(auto objectEntry = start; objectEntry != end ;objectEntry++){

    CurrentObject = objectEntry->second;

    WriteRegObjectFunctionStart(objectEntry->first);

    auto& memberList = objectEntry->second->MemberFunctions;
    auto& metaList = objectEntry->second->MetaFunctions;
   
    //Create a the members table for this object if it has any member functions defined and also store
    //the created table in the members list table thats on the Lua stack at mtList+1
    if(CurrentObject->NeedsMemberTable){

      if(CurrentObject->ObjectType == Object_CData){
        output << "  int memberTable = GetOrCreateTable(L, LUA_GLOBALSINDEX,\"" << objectEntry->first << "_FFIIndex\");\n";
      }else{
        output << "  int memberTable = GetOrCreateTable(L, LUA_GLOBALSINDEX,\"" << objectEntry->first << "\");\n";
      }
    }

    //Create a the metatable for this object if it has any metamethods defined also store the created 
    //table in the metatable list table thats on the Lua stack at the index contained in mtList
    if(metaList.size() != 0){
      if(CurrentObject->ObjectType == Object_CData){
        //WriteCDataMtCreate(metaList.size()*2, "(libFlags >> 16)");
        WriteTableCreate("metaTable", metaList.size(), "LUA_GLOBALSINDEX", objectEntry->first+"MT", 0);
      }else{
        output << "  int metaTable = GetOrCreateTable(L, LUA_REGISTRYINDEX,\"" << objectEntry->first << "\");\n";
      }
    }

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

    //clear the memberTable and/or metaTable tables off the stack if they were created for this object since were
    //at the end of this objects registration function
    if(memberList.size() != 0 && metaList.size() != 0){
      output << "  lua_pop(L, 2);\n";
    }else{
      output << "  lua_pop(L, 1);\n";
    }

    output << "}\n\n";
  }

  output << "extern int MTListMarker, MembersListMarker;\n\n";

  //build the main exported registration function that calls all the other object registration functions
  output << "void Register_LuaLib(lua_State* L, uint32_t options){\n\n";


  if(CollectedMacros->GobalFunctions.size() != 0){
    
    for each (RecordEntry* var in CollectedMacros->GobalFunctions){
      WriteFunctionInit(*var, "libTable", 0);
    }
  }

  //emit all the calls to the object registration functions we created earlier
  for(auto objectEntry = start; objectEntry != end ;objectEntry++){
    if(objectEntry->second->ObjectType != Object_CData){
      output << "  Register_" << objectEntry->first << "(L, options);\n";
    }
  }

  output << "  lua_pop(L, 2);\n}\n\n";
  
  output.flush();
}