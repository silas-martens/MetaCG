/**
 * File: SymbolRetriever.cpp
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 */

#include "SymbolRetriever.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace SymbolRetriever {
struct RemoveEnvInScope {
  explicit RemoveEnvInScope(const char* varName) : varName(varName) {
    oldVal = getenv(varName);
    if (oldVal)
      setenv(varName, "", true);
  }

  ~RemoveEnvInScope() {
    if (oldVal)
      setenv(varName, oldVal, true);
  }

 private:
  const char* varName;
  const char* oldVal;
};

std::string getExecPath() {
  RemoveEnvInScope removePreload("LD_PRELOAD");
  char filename[512] = {0};
  auto n = readlink("/proc/self/exe", filename, sizeof(filename) - 1);
  if (n > 0) {
    return filename;
  }
  return "";
}

std::vector<MemMapEntry> readMemoryMap() {
  RemoveEnvInScope removePreload("LD_PRELOAD");

  std::vector<MemMapEntry> entries;

  std::ifstream memory_map("/proc/self/maps");
  if (!memory_map.is_open()) {
    std::cout << "Could not load memory map.\n";
    return entries;
  }

  std::string addrRange;
  std::string perms;
  uint64_t offset;
  std::string dev;
  uint64_t inode;
  std::string path;

  std::string line;
  while (std::getline(memory_map, line)) {
    std::istringstream lineStream(line);
    lineStream >> addrRange >> perms >> std::hex >> offset >> std::dec >> dev >> inode >> path;

    // Skip entries that are not "r-xp" or contain certain characters
    if (perms != "r-xp" || path.find("[") != std::string::npos) {
      continue;
    }

    uintptr_t addrBegin = std::stoul(addrRange.substr(0, addrRange.find('-')), nullptr, 16);
    entries.push_back({path, addrBegin, offset});
  }

  memory_map.close();
  return entries;
}

std::vector<std::string> readSharedObjectDependencies(const std::string& exec_file) {
  RemoveEnvInScope removePreload("LD_PRELOAD");
  std::vector<std::string> dsoFiles;

  std::string command = "ldd " + exec_file;

  char buffer[256];
  FILE* output = popen(command.c_str(), "r");
  if (!output) {
    std::cout << "Could not execute ldd.\n";
    return {};
  }

  std::string entry;
  std::string arrow;
  std::string entryPath;
  while (fgets(buffer, sizeof(buffer), output)) {
    std::istringstream lineStream(buffer);
    lineStream >> entry >> arrow;
    if (strcmp(arrow.c_str(), "=>") != 0) {
      continue;
    }
    lineStream >> entryPath;
    dsoFiles.push_back(entryPath);
  }
  pclose(output);
  return dsoFiles;
}

SymbolTable loadSymbolTable(const std::string& object_file) {
  // Need to disable LD_PRELOAD, otherwise this library will be loaded in popen call, if linked dynamically
  RemoveEnvInScope removePreload("LD_PRELOAD");

  std::string command = "nm --defined-only ";
  if (object_file.find(".so") != std::string::npos) {
    command += "-D ";
  }
  command += object_file;

  char buffer[256] = {0};
  FILE* output = popen(command.c_str(), "r");
  if (!output) {
    std::cout << "Unable to execute nm to resolve symbol names.\n";
    return {};
  }

  SymbolTable table;

  uintptr_t addr;
  std::string symType;
  std::string symName;

  while (fgets(buffer, sizeof(buffer), output)) {
    std::istringstream line(buffer);
    if (buffer[0] != '0') {
      continue;
    }
    line >> std::hex >> addr;
    line >> symType;
    line >> symName;
    table[addr] = symName;
  }
  pclose(output);

  if (table.empty()) {
    std::cout << "Unable to resolve symbol names for binary " << object_file << "\n";
  }

  return table;
}

std::string getELFType(const std::string& object_file) {
  // Need to disable LD_PRELOAD, otherwise this library will be loaded in popen call, if linked dynamically
  RemoveEnvInScope removePreload("LD_PRELOAD");

  std::string command = "llvm-readelf -h ";
  command += object_file;
  command += " | grep Type";

  char buffer[256] = {0};
  FILE* output = popen(command.c_str(), "r");
  if (!output) {
    std::cout << "Unable to execute llvm-readelf.\n";
    return {};
  }

  std::string desc;
  std::string type;

  if (fgets(buffer, sizeof(buffer), output)) {
    std::istringstream line(buffer);
    line >> desc >> type;
  } else {
    std::cout << "Output buffer is empty\n";
  }
  return type;
}

SymbolSetList loadSymbolSets(const std::string& execFile) {
  SymbolSetList symSets;

  // Load symbols from main executable
  auto execSyms = loadSymbolTable(execFile);
  if (execSyms.empty()) {
    return {};
  }
  auto dsoList = readSharedObjectDependencies(execFile);
  symSets.reserve(dsoList.size() + 1);
  symSets.emplace_back(execFile, getSymbolSet(execSyms));

  for (auto& entry : dsoList) {
    auto table = loadSymbolTable(entry);
    symSets.emplace_back(entry, getSymbolSet(table));
  }
  return symSets;
}

SymTableList loadAllSymTables(const std::string& execFile) {
  std::vector<std::pair<std::string, SymbolTable>> symTables;

  // Load symbols from main executable
  auto execSyms = loadSymbolTable(execFile);

  auto dsoList = readSharedObjectDependencies(execFile);
  symTables.reserve(dsoList.size() + 1);
  symTables.emplace_back(execFile, execSyms);

  for (auto& entry : dsoList) {
    auto table = loadSymbolTable(entry);
    symTables.emplace_back(entry, table);
  }
  return symTables;
}

MappedSymTableMap loadMappedSymTables(const std::string& execFile, bool printDebug) {
  MappedSymTableMap addrToSymTable;

  // Load symbols from executable and shared libs
  auto memMap = readMemoryMap();
  for (auto& entry : memMap) {
    // Executable starts at address 0x0
    if (addrToSymTable.empty()) {
      auto elfType = getELFType(execFile);
      if (elfType == "EXEC") {
        entry.addrBegin = 0;
        entry.offset = 0;
      }
      if (printDebug) {
        std::cout << "ELF type: " << elfType << "\n";
      }
    }
    auto& filename = entry.path;
    auto table = loadSymbolTable(filename);
    if (table.empty()) {
      std::cout << "Could not load symbols from " << filename << "\n";

      continue;
    }

    if (printDebug) {
      std::cerr << "Loaded " << table.size() << " symbols from " << filename << "\n";
      std::cerr << " > Starting address: 0x" << std::hex << entry.addrBegin << "\n";
      std::cerr << " > Offset: 0x" << entry.offset << std::dec << "\n";
    }
    MappedSymTable mappedTable{std::move(table), entry};
    addrToSymTable[entry.addrBegin] = mappedTable;
  }
  return addrToSymTable;
}

std::string findSymbol(uint64_t addrInProc, MappedSymTableMap& mappedSymTables) {
  auto nextHighestIt = mappedSymTables.upper_bound(addrInProc);
  if (nextHighestIt == mappedSymTables.begin()) {
    return "";
  }
  nextHighestIt--;
  const auto& symbolTable = nextHighestIt->second;
  auto addrInObj = mapAddrToObj(addrInProc, symbolTable);
  auto it = symbolTable.table.find(addrInObj);
  if (it != symbolTable.table.end()) {
    return it->second;
  }
  return "";
}
}  // namespace SymbolRetriever
