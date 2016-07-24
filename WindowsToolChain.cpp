//===--- ToolChains.cpp - ToolChain Implementations -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Arg.h"
#include "clang/Driver/ArgList.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "clang/Basic/Version.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "clang/Lex/HeaderSearchOptions.h"

#include "llvm/Support/FileSystem.h"

// Include the necessary headers to interface with the Windows registry and
// environment.
#ifdef _MSC_VER
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #define NOMINMAX
  #include <Windows.h>
#endif

using namespace clang::driver;
using namespace clang;

static HKEY GetRootKeyType(const StringRef& keyPath,  StringRef& subKey){
  
  if (keyPath.startswith("HKEY_CLASSES_ROOT\\")){
    subKey = keyPath.substr(18);
    return HKEY_CLASSES_ROOT;
  }else if(keyPath.startswith("HKEY_USERS\\")){
    subKey = keyPath.substr(11);
    return HKEY_USERS;
  }else if(keyPath.startswith("HKEY_LOCAL_MACHINE\\")){
    subKey = keyPath.substr(19);
    return HKEY_LOCAL_MACHINE;
  }else if(keyPath.startswith("HKEY_CURRENT_USER\\")){
    subKey = keyPath.substr(18);
    return HKEY_CURRENT_USER;
  }else{
    return NULL;
  }
}

static bool FetchRegStringValue(HKEY hKey, const char *valueName, std::string& result){
 
  char valueBuf[250];
  DWORD valueSize = sizeof(valueBuf)-1;
  DWORD valueType;
  
  long lResult = RegQueryValueExA(hKey, valueName, NULL, &valueType, (LPBYTE)&valueBuf, &valueSize);
  if(lResult != ERROR_SUCCESS)return false;
  
  //let std::string add the terminating 0 at the end
  result.assign(valueBuf, valueSize-1);

  return true;
}

static bool FetchRegKeyStringValue(HKEY hRootKey, const char *keyPath, const char *valueName, std::string& result){
  bool returnValue = false;
  HKEY hKey = NULL;

  long lResult = RegOpenKeyEx(hRootKey, keyPath, 0, KEY_READ, &hKey);
  
  if(lResult == ERROR_SUCCESS){
    returnValue = FetchRegStringValue(hKey, valueName, result);

    RegCloseKey(hKey);
  }

  return returnValue;
}

const char versionString[] = "$VERSION";

/// \brief Read registry string.
/// This also supports a means to look for high-versioned keys by use
/// of a $VERSION placeholder in the key path.
/// $VERSION in the key path is a placeholder for the version number,
/// causing the highest value path to be searched for and used.
/// I.e. "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\$VERSION".
/// There can be additional characters in the component.  Only the numberic
/// characters are compared.
static bool getSystemRegistryString(const StringRef& keyPath, const char *valueName, std::string& result, const std::string& requiredSubDir = std::string()) {

  using namespace llvm::sys;

  HKEY hKey = NULL;
  StringRef subKey;
  long lResult;
  bool returnValue = false;

  HKEY hRootKey = GetRootKeyType(keyPath, subKey);
  
  int placeHolderIndex = subKey.find("$VERSION");

  // If we have a $VERSION placeholder, do the highest-version search.
  if(placeHolderIndex != StringRef::npos) {
    std::string baseKey = subKey.substr(0, placeHolderIndex);

    HKEY hTopKey = NULL;
    lResult = RegOpenKeyEx(hRootKey, baseKey.c_str(), 0, KEY_READ, &hTopKey);

    if (lResult == ERROR_SUCCESS) {
      char keyName[256];
      int bestIndex = -1;
      double bestValue = 0.0;
      DWORD index, size = sizeof(keyName) - 1;

      std::string keyValue, bestPath;

      for (index = 0; RegEnumKeyEx(hTopKey, index, keyName, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; index++) {
        const char *sp = keyName;
        while (*sp && !isdigit(*sp))sp++;

        if (!*sp)continue;

        const char *ep = sp + 1;
        while (*ep && (isdigit(*ep) || (*ep == '.')))ep++;

        keyName[size] = 0;

        std::string versionedPath = baseKey+keyName;

        if(!FetchRegKeyStringValue(hRootKey, versionedPath.data(), valueName, keyValue))continue;

        if(requiredSubDir.empty()){
          if(!fs::exists(keyValue)){
            continue;
          }
        }else{
          SmallVector<char, 255> pathBuffer;

          path::append(pathBuffer, keyValue, requiredSubDir);

          if(!fs::exists(pathBuffer.data())){
            continue;
          }
        }

        char numBuf[32];
        strncpy(numBuf, sp, sizeof(numBuf) - 1);
        numBuf[sizeof(numBuf) - 1] = '\0';
        double value = strtod(numBuf, NULL);
        
        if(value > bestValue) {
          bestIndex = (int)index;
          bestValue = value;
          bestPath = keyValue;
        }
        size = sizeof(keyName) - 1;
      }
      
      RegCloseKey(hTopKey);

      if(bestPath.empty())return false;

       result = bestPath;
      return true;
    }
  } else {
    return FetchRegKeyStringValue(hRootKey, subKey.data(), valueName, result);
  }

  return returnValue;
}

/// \brief Get Windows SDK installation directory.
static bool getWindowsSDKDir(std::string &path) {

  // Try the Windows registry.
  bool hasSDKDir = getSystemRegistryString("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows\\$VERSION",
                                           "InstallationFolder", path, "include");

  // If we have both vc80 and vc90, pick version we were compiled with.
  return hasSDKDir && !path.empty();
}

void AdjustStudioDir(std::string& VSDir){

  if(VSDir.back() == '\\')VSDir.pop_back();

  int index = VSDir.find("\\Common7\\IDE");

  if(index != std::string::npos){
    VSDir.resize(index);
  }
}

  // Get Visual Studio installation directory default to the highests unless the version is supplied
static bool getVisualStudioDir(std::string &path, const char* version = NULL) {
  // First check the environment variables that vsvars32.bat sets.
  const char* vcinstalldir = getenv("VCINSTALLDIR");
  
  if(vcinstalldir){
    char *p = const_cast<char *>(strstr(vcinstalldir, "\\VC"));
    if (p)
      *p = '\0';
    path = vcinstalldir;
    return true;
  }

  std::string versionString = version == NULL ? "$VERSION" : version;

  std::string vsIDEInstallDir;
  // Then try the windows registry.
  bool hasVCDir = getSystemRegistryString(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\"+versionString,
    "InstallDir", vsIDEInstallDir);

  // If we have both vc80 and vc90, pick version we were compiled with.
  if (hasVCDir && !vsIDEInstallDir.empty()) {
    AdjustStudioDir(vsIDEInstallDir);
    path = vsIDEInstallDir;
    return true;
  }

  std::string vsExpressIDEInstallDir;

  bool hasVCExpressDir = getSystemRegistryString(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VCExpress\\"+versionString,
    "InstallDir", vsExpressIDEInstallDir, "include");

  if(hasVCExpressDir && !vsExpressIDEInstallDir.empty()) {
     AdjustStudioDir(vsExpressIDEInstallDir);
     path = vsExpressIDEInstallDir;
    return true;
  }

  if(version != NULL){
    return false;
  }

  // Try the environment.
  const char *vs100comntools = getenv("VS100COMNTOOLS");
  const char *vs90comntools = getenv("VS90COMNTOOLS");
  const char *vs80comntools = getenv("VS80COMNTOOLS");
  const char *vscomntools = NULL;

  // Try to find the version that we were compiled with
  if(false) {}
  #if (_MSC_VER >= 1600)  // VC100
  else if(vs100comntools) {
    vscomntools = vs100comntools;
  }
  #elif (_MSC_VER == 1500) // VC80
  else if(vs90comntools) {
    vscomntools = vs90comntools;
  }
  #elif (_MSC_VER == 1400) // VC80
  else if(vs80comntools) {
    vscomntools = vs80comntools;
  }
  #endif
  // Otherwise find any version we can
  else if (vs100comntools)
    vscomntools = vs100comntools;
  else if (vs90comntools)
    vscomntools = vs90comntools;
  else if (vs80comntools)
    vscomntools = vs80comntools;

  if (vscomntools && *vscomntools) {
    const char *p = strstr(vscomntools, "\\Common7\\Tools");
    path = p ? std::string(vscomntools, p) : vscomntools;
    return true;
  }
  return false;
}


void addSystemInclude(clang::HeaderSearchOptions& headerSearchOptions, std::string& path){
  headerSearchOptions.AddPath(path, clang::frontend::System, false, false);
}

void addSystemInclude(clang::HeaderSearchOptions& headerSearchOptions, StringRef& path){
  headerSearchOptions.AddPath(path, clang::frontend::System, false, false);
}

void AddClangSystemIncludeArgs(clang::HeaderSearchOptions& headerSearch, const std::string& windowsSDKVer, const char* vsVersion) {

#ifdef _MSC_VER
  // Honor %INCLUDE%. It should know essential search paths with vcvarsall.bat.
  if (const char *cl_include_dir = getenv("INCLUDE")) {
    SmallVector<StringRef, 8> Dirs;
    StringRef(cl_include_dir).split(Dirs, ";");
    int n = 0;
    for (SmallVectorImpl<StringRef>::iterator I = Dirs.begin(), E = Dirs.end();
         I != E; ++I) {
      StringRef d = *I;
      if (d.size() == 0)
        continue;
      ++n;
      addSystemInclude(headerSearch, d);
    }
    if (n) return;
  }

  std::string VSDir;
  std::string WindowsSDKDir;

  for (auto it = headerSearch.UserEntries.begin(); it != headerSearch.UserEntries.end(); it++){

    if(it->Path.find("\\VC\\include") != -1){
      headerSearch.UserEntries.erase(it);
      break;
    }
  }


  if(!windowsSDKVer.empty()){
    getSystemRegistryString(std::string("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows\\")+windowsSDKVer,
                               "InstallationFolder", WindowsSDKDir, "include");
  }

  // When built with access to the proper Windows APIs, try to actually find
  // the correct include paths first.
  if (getVisualStudioDir(VSDir, vsVersion)) {
    addSystemInclude(headerSearch, VSDir + "\\VC\\include");
    
    if(WindowsSDKDir.empty())getWindowsSDKDir(WindowsSDKDir);
    if(WindowsSDKDir.back() == '\\' || WindowsSDKDir.back() == '/')WindowsSDKDir.pop_back();

    if(!WindowsSDKDir.empty()){
      addSystemInclude(headerSearch, WindowsSDKDir + "\\include");
    }else
      addSystemInclude(headerSearch, VSDir + "\\VC\\PlatformSDK\\Include");
    return;
  }
#endif // _MSC_VER

  // As a fallback, select default install paths.
  const StringRef Paths[] = {
    "C:/Program Files/Microsoft Visual Studio 10.0/VC/include",
    "C:/Program Files/Microsoft Visual Studio 9.0/VC/include",
    "C:/Program Files/Microsoft Visual Studio 9.0/VC/PlatformSDK/Include",
    "C:/Program Files/Microsoft Visual Studio 8/VC/include",
    "C:/Program Files/Microsoft Visual Studio 8/VC/PlatformSDK/Include"
  };
  //addSystemIncludes(DriverArgs, CC1Args, Paths);
}

