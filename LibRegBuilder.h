#pragma once

#include "MacroRecorder.h"
#include <iostream>
#include <fstream>

class LibRegBuilder{

public:
  LibRegBuilder(MacroCollector* collectedMacros, const std::string& outputPath);

  ~LibRegBuilder(){}

  void WriteLibReg();
  void WriteTableCreate(const std::string& tableName, int size, const std::string& destTable, const std::string& destKey);

  void WriteFunctionInit(RecordEntry& entry, const char* outputTable, int subNameStart);
  void WriteExtenList(std::vector<RecordEntry*>& functionList);
  void WriteRecorderArray(std::vector<RecordEntry*>& functionList);

  void WriteRegObjectFunctionStart(const std::string& objectName);

private:
  MacroCollector* CollectedMacros;
  ObjectData* CurrentObject;
  std::ofstream output;
};

