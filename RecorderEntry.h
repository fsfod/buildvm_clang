
#pragma once

#include <string>
#include <vector>
#include "llvm\ADT\StringRef.h"

using llvm::StringRef;

enum PushType{
  PushType_Invalid = 0,
  PushType_String,
  PushType_StackSlot,
  PushType_StackSlotBase,
  PushType_StaticMemberTable,
  PushType_MemberTable,
  PushType_MT,
  PushType_Global,
};

class PushEntry{

public:
  PushEntry() 
    :Type(PushType_Invalid), StackSlot(0){
  }

  PushEntry(const StringRef& strLiteral){
    StringLiteral = new std::string(strLiteral);
    Type = PushType_String;
  }
  
  PushEntry(PushType typeNum) 
    : Type(typeNum), StackSlot(0){
  }

  PushEntry(PushType typeNum, int stackSlot)
    : Type(typeNum), StackSlot(stackSlot){
  }

  PushEntry(int stackSlot){
    StackSlot = stackSlot;
    Type = PushType_StackSlot;
  }

public:
  PushType Type;
  union{
    std::string* StringLiteral;
    int StackSlot;
  };
};

enum RecorderType{
  Recorder_Default = 0,
  Recorder_GetField,
  Recorder_SetField,
};

class RecordEntry{

public:
  RecordEntry() : 
    FunctionId(-1), RecordLineNumber(-1), Valid(true), NeedsMembersTable(false), RequiredFlag(), Type(Recorder_Default), NoRecorderExtern(false), Name(){
  }

  void SetFunctionName(const std::string& newName){
    assert(Name.empty());

    Name = newName;
    
    if(!GetIsFieldSetterOrGetter() && TraceRecorder == "."){
      TraceRecorder = "recff_"+newName;
      RecorderFunctionName = TraceRecorder;
    }
  }

  bool GetIsFieldSetterOrGetter() const{
    return Type == Recorder_GetField || Type == Recorder_SetField;
  }

  //Does recorder function need to forward extern declaration since its header won't be included
  //in the lib registration cpp file
  bool GetRecorderExternNeeded() const{
    return !(NoRecorderExtern || GetIsFieldSetterOrGetter());
  }

  void BuildFieldGetSet(const StringRef& objectType, const StringRef& fieldType, const StringRef& fieldOffset);

  std::string GetObjectName(){

    int underSlash = Name.find('_');

    if(underSlash == std::string::npos){
      return "";
    }

    return Name.substr(0, underSlash);
  }

public:
  RecorderType Type;
  bool Valid, NeedsMembersTable, NoRecorderExtern;

  int FunctionId, RecordLineNumber;
  std::string Name, TraceRecorder, RequiredFlag;
  std::string RecordOptions;
  std::vector<PushEntry> PushStack;
  
  std::string RecorderFunctionName;
  std::string RecorderLine;
};

enum Object_Type{
  Object_Unknown,
  Object_Userdata,
  Object_CData,
};

class ObjectRecorderData{

public:
  ObjectRecorderData(std::string& name) : 
    Name(name), NeedsFlagArg(false), NeedsMemberTable(false), ObjectType(Object_Unknown){
  }

  void AddMetaFunction(RecordEntry* entry){
    MetaFunctions.push_back(entry);

    FunctionAdded(entry);
  }

  void AddMemberFunction(RecordEntry* entry){
    MemberFunctions.push_back(entry);
    
    NeedsMemberTable = true;

    FunctionAdded(entry);
  }

private:
  void FunctionAdded(RecordEntry* entry){

    if(!entry->RequiredFlag.empty()){
      NeedsFlagArg = true;
    }
    
    //TODO handle when this is not a table for our object type and add early member table creation behaviour
    if(entry->NeedsMembersTable){
      NeedsMemberTable = true;
    }
  }

public:
  Object_Type ObjectType;
  bool NeedsFlagArg, NeedsMemberTable;
  std::string Name;
  std::vector<RecordEntry*> MemberFunctions;
  std::vector<RecordEntry*> MetaFunctions;
};
