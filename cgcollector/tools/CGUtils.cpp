//
// Created by silasm on 31.10.24.
//

#include "Callgraph.h"
#include "cxxopts.hpp"
#include "io/VersionTwoMCGReader.h"
#include "io/VersionTwoMCGWriter.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace metacg;
bool handleOptions(int argc, char* argv[], std::string& inputFile, std::string& countReachableFromName,
                   bool& countReachableFromFlag, bool& countEdges) {
  try {
    // Define options using cxxopts
    cxxopts::Options options("program", "Program to analyze reachable functions from main");

    // Add options
    options.add_options()("c,countReachableFrom", "Counts number of reachable functions from arg",
                          cxxopts::value<std::string>())("e,countNumberOfEdges", "Counts number of edges",
                                                         cxxopts::value<bool>()->default_value("false"))(
        "input", "Input JSON file", cxxopts::value<std::string>());

    // Parse positional arguments
    options.parse_positional({"input"});

    // Parse the command-line arguments
    auto result = options.parse(argc, argv);

    if (result.count("countReachableFrom")) {
      countReachableFromName = result["countReachableFrom"].as<std::string>();
      countReachableFromFlag = true;
    }
    if (result.count("countNumberOfEdges"))
      countEdges = true;

    // Check if the input file is provided
    if (!result.count("input")) {
      std::cerr << "Error: Input file is required.\n";
      std::cout << options.help() << std::endl;
      return false;
    }

    // Get parsed values
    inputFile = result["input"].as<std::string>();

    return true;

  } catch (const cxxopts::OptionException& e) {
    std::cerr << "Error parsing options: " << e.what() << std::endl;
    return false;
  }
}

std::set<std::string> getAllOverriddenFunctions(nlohmann::json& cg, nlohmann::json& baseFn,
                                                const std::string& overridesKey) {
  std::set<std::string> overriddenSet;
  std::cout << "baseFn: " << baseFn.dump() << std::endl;
  auto overrides = baseFn[overridesKey];
  std::cout << "overrides: " << overrides.dump() << std::endl;
  std::for_each(overrides.begin(), overrides.end(),
                [&cg, &overriddenSet, &overridesKey](const std::string& overridesFn) {
                  if (!cg.contains(overridesFn)) {
                    std::cout << "Returns" << std::endl;
                    return;
                  }
                  auto& fnNode = cg[overridesFn];
                  overriddenSet.insert(overridesFn);
                  for (auto& entry : getAllOverriddenFunctions(cg, fnNode, overridesKey)) {
                    overriddenSet.insert(entry);
                  }
                });
  return overriddenSet;
}
// Function to count reachable functions from main
int countReachableFrom(const std::unique_ptr<metacg::Callgraph>& cg, const std::string& name, nlohmann::json& cgJson) {
  int numberOfReachableFromMain;
  // Deserialize graph

  // DFS
  auto mainNode = cg->getNode(name);

  std::unordered_set<size_t> visited;
  std::stack<size_t> stack;
  stack.push(mainNode->getId());

  while (!stack.empty()) {
    size_t currentNodeId = stack.top();
    stack.pop();

    // If not visited, process callees
    if (visited.find(currentNodeId) == visited.end()) {
      visited.insert(currentNodeId);
      auto callees = cg->getCallees(currentNodeId);

      for (const auto& callee : callees) {
        // Adding virtual callees to visited functions
        const auto& currentNode = callee;
        const auto& currentNodeName = currentNode->getFunctionName();

        const auto& overriddenFunctions = getAllOverriddenFunctions(cgJson, cgJson[currentNodeName], "overriddenBy");
        std::cout << "Adding " << overriddenFunctions.size() << "new visited functions as child of "
                  << currentNode->getFunctionName() << "\n";
        for (auto& overriddenFunction : overriddenFunctions) {
          if (visited.find(cg->getNode(overriddenFunction)->getId()) == visited.end()) {
            stack.push(cg->getNode(overriddenFunction)->getId());
          }
        }

        if (visited.find(callee->getId()) == visited.end())
          stack.push(callee->getId());
      }
    }
  }

  return visited.size();
}

int countNumberOfEdges(const std::unique_ptr<metacg::Callgraph>& cg) { return cg->getEdges().size(); }

void printEdges(const std::unique_ptr<metacg::Callgraph>& cg) {
  auto EdgeContainer = cg->getEdges();

  std::ofstream outFile("output.txt");

  // Check if the file was opened successfully
  if (!outFile) {
    std::cerr << "Error opening file for writing." << std::endl;
    exit(1);
  }

  for (const auto& e : EdgeContainer) {
    outFile << cg->getNode(e.first.first)->getFunctionName() << " calls "
            << cg->getNode(e.first.second)->getFunctionName() << "\n";
  }
}
int main(int argc, char** argv) {
  // Parse command-line arguments
  std::string inputFile;
  std::string countReachableName;
  bool countEdgesFlag = false;
  bool countReachableFlag = false;

  // Option parsing
  if (!handleOptions(argc, argv, inputFile, countReachableName, countReachableFlag, countEdgesFlag)) {
    return 1;  // Exit if options were not parsed successfully
  }

  // Read input file
  std::ifstream infile(inputFile);
  if (!infile.is_open()) {
    std::cout << "[Error] Could not open mergedCallGraph.json.\n";
  }

  // Deserialize call-graph
  nlohmann::json jsonFile;
  infile >> jsonFile;
  metacg::io::JsonSource jsonSource(jsonFile);
  metacg::io::VersionTwoMetaCGReader mcgReader(jsonSource);
  std::unique_ptr<Callgraph> cg = mcgReader.read();

  if (false) {
    std::cout << "Printing edges for " << inputFile << "\n";
    printEdges(cg);
    exit(0);
  }
  // Check if the flag is set
  if (countReachableFlag) {
    int count = countReachableFrom(cg, countReachableName, jsonFile["_CG"]);
    std::cout << "Number of reachable functions from main: " << count << std::endl;
  }

  if (countEdgesFlag) {
    int count = countNumberOfEdges(cg);
    std::cout << "Number of Edges: " << count << std::endl;
  }
  return 0;
}
