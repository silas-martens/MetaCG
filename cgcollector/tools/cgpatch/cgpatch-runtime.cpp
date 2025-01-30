/**
 * File: cgpatch-runtime.cpp
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 */

#include "Callgraph.h"
#include "SymbolRetriever.h"
#include "io/VersionTwoMCGReader.h"
#include "io/VersionTwoMCGWriter.h"
#include "nlohmann/json.hpp"
#include <cstdlib>
#include <iostream>
#if USE_MPI == 1
#include <mpi.h>
#endif
// applications do not require mpi.
#include <fstream>

using namespace SymbolRetriever;
namespace {
// Variables
std::unique_ptr<metacg::Callgraph> globalCallgraph;
MappedSymTableMap symTables;
bool shouldWrite = true;
int counter;

//Logger
spdlog::logger* console;
spdlog::logger* errConsole;
// Struct responsible for initialization and finalization of the patch-graph
struct ValidatorInitializer {
  ValidatorInitializer() {
    globalCallgraph = std::make_unique<metacg::Callgraph>();
    console = metacg::MCGLogger::instance().getConsole();
    errConsole = metacg::MCGLogger::instance().getErrConsole();
    counter = 0;
  }

  ValidatorInitializer(const ValidatorInitializer&) = delete;
  ValidatorInitializer& operator=(const ValidatorInitializer&) = delete;

  ValidatorInitializer(ValidatorInitializer&&) = delete;
  ValidatorInitializer& operator=(ValidatorInitializer&&) = delete;

  ~ValidatorInitializer() {
    // Write Callgraph to file
    if (shouldWrite) {
      metacg::io::VersionTwoMCGWriter mcgWriter;
      metacg::io::JsonSink jsonSink;
      mcgWriter.write(globalCallgraph.get(), jsonSink);
      nlohmann::json j = jsonSink.getJson();

      const char* filename = std::getenv("CGPATCH_CG_NAME");
      if(!filename) {
        filename = "validateGraph.json";
      }
      std::ofstream ofs(filename);
      if (ofs.is_open()) {
        ofs << j;
        ofs.close();
      } else {
        errConsole->error("Unable to open file validateGraph.json for writing.");
      }
    }
  }

};  // _validator_init_finalize;
}  // namespace

extern "C" void __metacg_indirect_call(const char* name, void* address) {
  static ValidatorInitializer validator_init;
  // resolve name
  if (symTables.empty()) {  // Loads symTables if symTables is not initialized yet. This potentially runs before the
                            // static constructor
    symTables = loadMappedSymTables(getExecPath());
  }

  const std::string& symbol = findSymbol(reinterpret_cast<std::uintptr_t>(address), symTables);
  if (symbol.empty()) {
    errConsole->error("Could not find symbol for address {:#x}", reinterpret_cast<std::uintptr_t>(address));
    return;
  }
  // Add new edge if edge does not exist yet
  if (!globalCallgraph->existEdgeFromTo(name, symbol)) {
    auto caller = globalCallgraph->getOrInsertNode(name);
    auto callee = globalCallgraph->getOrInsertNode(symbol);

    globalCallgraph->addEdge(caller, callee);
    counter++;

    return;
  }
}

#if USE_MPI == 1
// Overwrite MPI_Finalize using PMPI interface
/*
extern "C" int MPI_Comm_rank(void*, int*) __attribute__((weak));
extern "C" int MPI_Comm_size(void*, int*) __attribute__((weak));
extern "C" void* MPI_COMM_WORLD __attribute__((weak));
extern "C" int MPI_Barrier(void*) __attribute__((weak));
extern "C" int PMPI_Finalize(void) __attribute__((weak));
extern "C" int MPI_Abort(void*, int) __attribute__((weak));
*/

extern "C" int MPI_Finalize(void) {
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // serialize call-graph
  metacg::io::VersionTwoMCGWriter mcgWriter;
  metacg::io::JsonSink jsonSink;
  mcgWriter.write(globalCallgraph.get(), jsonSink);
  nlohmann::json j = jsonSink.getJson();

  std::string filename = "validateGraph_" + std::to_string(rank) + ".json";
  std::ofstream ofs(filename);
  if (ofs.is_open()) {
    ofs << j;
    ofs.close();
  } else {
    errConsole->error("Unable to open file {} for writing.", filename);
  }

  MPI_Barrier(MPI_COMM_WORLD);  // Wait to ensure that all MPI ranks have finished writing

  if (rank == 0) {
    std::string mergedFilename = "mergedCallGraph.json";
    std::string mergeCommand = "cgmerge " + mergedFilename;

    // Generating merge command
    for (int i = 0; i < size; ++i) {
      mergeCommand += " validateGraph_" + std::to_string(i) + ".json";
    }

    // Create null file
    {
      std::ofstream ofs(mergedFilename, std::ios::trunc);

      if (!ofs.is_open()) {
        errConsole->error("Unable to open file {} for writing", mergedFilename);
        return -1;
      }
      ofs << "null";
    }

    // Run merge command
    FILE* output = popen(mergeCommand.c_str(), "r");
    if (!output) {
      errConsole->error("Failed to execute cgmerge command.");
      MPI_Abort(MPI_COMM_WORLD, 1);  // Abort if cgmerge fails to execute
    }

    // Write the cgmerge output to std::cout
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), output) != nullptr) {
      std::cout << buffer;
    }
    pclose(output);

  } else {
    shouldWrite = false;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  // Delete temporary call-graphs
  if (std::remove(filename.c_str()) != 0) {
    errConsole->error("Rank {} failed to delete its temporary file: {}", rank, filename);
  }

  int PMPI_FINALIZE_STATUS = PMPI_Finalize();  // Finalize MPI environment

  // Read merged call-graph
  if (shouldWrite) {
    {
      std::ifstream infile("mergedCallGraph.json");
      if (!infile.is_open()) {
        console->info("Could not open mergedCallGraph.json.");
      }

      nlohmann::json jsonFile;
      infile >> jsonFile;

      metacg::io::JsonSource jsonSource(jsonFile);
      metacg::io::VersionTwoMetaCGReader mcgReader(jsonSource);
      globalCallgraph = mcgReader.read();
    }
    std::remove("mergedCallGraph.json");
  }
  return PMPI_FINALIZE_STATUS;
}

#endif
