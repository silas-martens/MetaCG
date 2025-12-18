#include "Callgraph.h"
#include "CgNode.h"
#include "CgTypes.h"
#include "ReachabilityAnalysis.h"
#include "DominatorAnalysis.h"
#include "io/MCGReader.h"
#include <cxxopts.hpp>
#include <iostream>


int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: cgquery <command> [options] <input_file>\n";
        return 1;
    }

    std::string command = argv[1];
    std::string cg_name = argv[argc-1];

    auto fs = metacg::io::FileSource(cg_name);
    std::unique_ptr<metacg::io::MCGReader> r1 = metacg::io::createReader(fs);
    auto cg = r1->read();

    // Build argv vector including program name for cxxopts
    std::vector<char*> args;
    args.push_back(argv[0]); // Program name
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    if (command == "postdom") {
        cxxopts::Options options("cgquery postdom", "Postdominators analysis");
        options.add_options()
            ("n,node", "Postdominators of which node", cxxopts::value<std::string>())
            ("e,exit", "Exit node", cxxopts::value<std::string>())
            ("h,help", "Print help");

        auto result = options.parse(static_cast<int>(args.size()), args.data());

        if (result.count("help")) {
            std::cout << options.help() << "\n";
            return 0;
        }

        std::string nodeName = result.count("node") ? result["node"].as<std::string>() : ""; 
        std::string exitNode = result.count("exit") ? result["exit"].as<std::string>() : "";
        auto* nodePtr = cg->getFirstNode(nodeName);
        if (!nodePtr) {
            std::cerr << "Node '" << nodeName << "' not found in CG.\n";
            return 1;
        }

        auto* exitPtr = cg->getFirstNode(exitNode);
        if (!exitPtr) {
            std::cerr << "Node '" << exitNode << "' not found in CG.\n";
        }

        auto postdom = metacg::analysis::computeDoms<
            metacg::CgNode,
            metacg::Callgraph,
            metacg::analysis::TraverseDir::Backward>(*cg, *exitPtr);

        const auto& data = postdom[nodePtr];
        if (!data.initialized) {
            std::cerr << "Postdominator analysis not initialized for node '" << nodeName << "'\n";
            return 1;
        }

        std::cout << "Postdominators of " << nodeName << ":\n";
        for (const auto* pdPtr : data.Doms) {
            std::cout << "  " << pdPtr->getFunctionName() << "\n";
        }
    }
    else if (command == "dom") {
        cxxopts::Options options("cgquery doms", "Dominators analysis");
        options.add_options()
            ("n,node", "Dominators of which node", cxxopts::value<std::string>())
            ("e,entry", "Entry node", cxxopts::value<std::string>())
            ("h,help", "Print help");

        auto result = options.parse(static_cast<int>(args.size()), args.data());

        if (result.count("help")) {
            std::cout << options.help() << "\n";
            return 0;
        }

        std::string nodeName = result.count("node") ? result["node"].as<std::string>() : options.help();
        std::string entry = result.count("node") ? result["node"].as<std::string>() : "main";
        auto* nodePtr = cg->getFirstNode(nodeName);
        if (!nodePtr) {
            std::cerr << "Node '" << nodeName << "' not found in CG.\n";
            return 1;
        }

        std::string entryName = result.count("entry") ? result["entry"].as<std::string>() : "main";
        auto* entryPtr = cg->getFirstNode(entryName);
        if (!entryPtr) {
            std::cerr << "Entry node '" << entryName << "' not found in CG.\n";
            return 1;
        }

        auto dom = metacg::analysis::computeDoms<
            metacg::CgNode,
            metacg::Callgraph,
            metacg::analysis::TraverseDir::Forward>(*cg, *entryPtr);

        const auto& data = dom[nodePtr];
        if (!data.initialized) {
            std::cerr << "Dominator analysis not initialized for node '" << nodeName << "'\n";
            return 1;
        }

        std::cout << "Dominators of " << nodeName << ":\n";
        for (const auto* pdPtr : data.Doms) {
            std::cout << "  " << pdPtr->getFunctionName() << "\n";
        }
    }

    return 0;
}

