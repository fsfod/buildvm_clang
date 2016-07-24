#pragma once

#include "MacroRecorder.h"
#include <iostream>
#include <fstream>

class LibRegBuilder{

public:
  LibRegBuilder(RecorderCollection* collectedMacros, const std::string& outputPath);

  ~LibRegBuilder(){}

  bool RecordersValid();

  void WriteLibReg(std::vector<std::string>& includeList);
  void WriteTableCreate(const std::string& tableName, int size, const std::string& destTable, const std::string& destKey, int arraySize = 0);
  void WriteCDataMtCreate(int size, const std::string& typeId);

  void WriteFunctionInit(RecordEntry& entry, const char* outputTable, int subNameStart);
  void WriteExtenList(std::vector<RecordEntry*>& functionList);
  void WriteRecorderArray(std::vector<RecordEntry*>& functionList);

  void WriteRegObjectFunctionStart(const std::string& objectName);

private:
  RecorderCollection* CollectedMacros;
  ObjectRecorderData* CurrentObject;
  std::ofstream output;
};

