#include "ReachabilityAnalysis.h"
#include "DominatorAnalysis.h"
#include "CgTypes.h"
#include "Callgraph.h"
#include "CgNode.h"
#include "io/MCGReader.h"
#include <cxxopts.hpp>
#include <iostream>


metacg::CgNode* getNodeOrError(metacg::Callgraph* cg, const std::string& name) {
    auto* ptr = cg->getFirstNode(name);
    if (!ptr) {
        std::cerr << "Node '" << name << "' not found in CG.\n";
        exit(2);
    }
    return ptr;
}

int handleDomCommand(
    metacg::Callgraph* cg,
    const std::vector<char*>& args,
    const std::string& commandName,
    const std::string& desc,
    metacg::analysis::TraverseDir dir,
    const std::string& targetOptionName,
    const std::string& refOptionName)
{
    cxxopts::Options options("cgquery " + commandName, desc);
    options.add_options()
        ("n,node", "Target node", cxxopts::value<std::string>())
        ("e," + refOptionName, (dir == metacg::analysis::TraverseDir::Forward ? "Entry node" : "Exit node"), cxxopts::value<std::string>())
        ("h,help", "Print help");


    auto result = options.parse(static_cast<int>(args.size()), args.data());

    if (result.count("help")) {
        std::cout << options.help() << "\n";
        return 0;
    }

    if (!result.count(targetOptionName) || !result.count(refOptionName)) {
        std::cerr << "Specify target node and " << refOptionName << " node.\n";
        std::cerr << options.help();
        return 2;
    }

    std::string nodeName = result[targetOptionName].as<std::string>();
    std::string refName = result[refOptionName].as<std::string>();

    auto* nodePtr = getNodeOrError(cg, nodeName);
    auto* refPtr  = getNodeOrError(cg, refName);

    std::unordered_map<const metacg::CgNode*, metacg::analysis::DomData<metacg::CgNode>> dom;
    if (dir == metacg::analysis::TraverseDir::Forward) {
        dom = metacg::analysis::computeDoms<
            metacg::CgNode,
            metacg::Callgraph,
            metacg::analysis::TraverseDir::Forward>(*cg, *refPtr);
    }
    else {
        dom = metacg::analysis::computeDoms<
            metacg::CgNode,
            metacg::Callgraph,
            metacg::analysis::TraverseDir::Backward>(*cg, *refPtr);
    }

    const auto& data = dom[nodePtr];
    if (!data.initialized) {
        std::cerr << commandName << " analysis not initialized for node '" << nodeName << "'\n";
        return 1;
    }

    for (const auto* dPtr : data.Doms) {
        std::cout << dPtr->getFunctionName() << "\n";
    }

    return 0;
}



int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: cgquery <command> [options] <input_file>\n";
        return 1;
    }

    std::string command = argv[1];
    std::string cg_name = argv[argc-1];

    if (command == "help" || command == "-h" || command == "--help") {
        std::cout << "Usage: cgquery <command> [options] <input_file>\n";
        std::cout << "Available commands:\n";
        std::cout << "  reaches   Query reachable nodes or check reachability\n";
        std::cout << "  doms      Query dominators of a node for a given entry node";
        std::cout << "  postdoms  Query postdominators of a node for a given exit node";
        return 0;
    }

    if (argc < 3) {
        std::cerr << "Need to specify command and input file." << std::endl;
        std::cerr << "Use cgquery help for available commands." << std::endl;
        return 1;
    }
    auto fs = metacg::io::FileSource(cg_name);
    std::unique_ptr<metacg::io::MCGReader> r1 = metacg::io::createReader(fs);
    auto cg = r1->read();

    // Build argv vector including program name for cxxopts
    std::vector<char*> args;
    args.push_back(argv[0]); // Program name
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    if (command == "reaches") {
        cxxopts::Options options("cgquery reaches", "Query reachable nodes or check if a node is reachable from a certain node");
        options.add_options()
            ("s, source", "Start function. Lists all functions reachable from this node.", cxxopts::value<std::string>())
            ("t, to", "If given, only checks whether this function is reachable from --source.", cxxopts::value<std::string>())
            ("h,help", "Print help");

        auto result = options.parse(static_cast<int>(args.size()), args.data());

        if (result.count("help")) {
            std::cout << options.help() << "\n";
            return 0;
        }

        if (!result.count("source")) {
            std::cerr << "Please specify the source node" << std::endl;
            std::cout << options.help() << std::endl;
            return 2;
        }



        auto* sourceNode = getNodeOrError(cg.get(), result["source"].as<std::string>());

        metacg::analysis::ReachabilityAnalysis reachabilityAnalysis(cg.get());

        if (result.count("to")) {
            std::string toName = result["to"].as<std::string>();

            auto* toNode = getNodeOrError(cg.get(), toName);

            return reachabilityAnalysis.existsPathBetween(sourceNode, toNode, true) ? 0 : 1;
        }
        else {
            auto reachableNodes = reachabilityAnalysis.getReachableNodesFrom(sourceNode, true);
            std::cout << "Reachable nodes:\n";

            for (const auto n : reachableNodes) {
                std::cout << n->getFunctionName() << '\n';
            }

            return 0;
        }
    }
    if (command == "dom") {
        return handleDomCommand(cg.get(), args, "dom", "Dominator analysis", metacg::analysis::TraverseDir::Forward, "node", "entry");
    } else if (command == "postdom") {
        return handleDomCommand(cg.get(), args, "postdom", "Postdominator analysis", metacg::analysis::TraverseDir::Backward, "node", "exit");
    }

    return 0;
}
