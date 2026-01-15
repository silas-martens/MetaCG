#include "DominatorAnalysis.h"
#include "Callgraph.h"
#include "CgNode.h"
#include "CgTypes.h"
#include "LoggerUtil.h"
#include "ReachabilityAnalysis.h"
#include "io/MCGReader.h"

#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <ostream>

metacg::CgNode* getNodeOrError(metacg::Callgraph* cg, const std::string& name) {
    auto* ptr = cg->getFirstNode(name);
    if (!ptr) {
        throw std::runtime_error("Node '" + name + "' not found in CG.");
    }
    return ptr;
}

int handleDomCommand(
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
        ("input", "Call graph file", cxxopts::value<std::string>())
        ("h,help", "Print help");

    options.parse_positional({"input"});
    options.positional_help("<input_file>");

    try {
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

        std::string cg_name = result["input"].as<std::string>();
        auto fs = metacg::io::FileSource(cg_name);
        auto reader = metacg::io::createReader(fs);
        std::unique_ptr<metacg::Callgraph> cg = reader->read();

        std::string nodeName = result[targetOptionName].as<std::string>();
        std::string refName = result[refOptionName].as<std::string>();

        auto* nodePtr = getNodeOrError(cg.get(), nodeName);
        auto* refPtr  = getNodeOrError(cg.get(), refName);

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
    }
    catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << options.help() << "\n";
        return 1;
    }

    return 0;
}

bool is_number(const std::string& s)
{
    return !s.empty() &&
           std::all_of(s.begin(), s.end(),
               [](unsigned char c) { return std::isdigit(c); });
}

int cgQueryMain(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: cgquery <command> [options] <input_file>\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "help" || command == "-h" || command == "--help") {
        std::cout << "Usage: cgquery <command> [options] <input_file>\n";
        std::cout << "Available commands:\n";
        std::cout << "  reaches    Query reachable nodes or check reachability\n";
        std::cout << "  dom    Query dominators of a node for a given entry node\n";
        std::cout << "  postdom    Query postdominators of a node for a given exit node\n";
        std::cout << "  help    Show this help";
        return 0;
    }

    if (argc < 3) {
        std::cerr << "Need to specify command and input file." << std::endl;
        std::cerr << "Use cgquery help for available commands." << std::endl;
        return 1;
    }
    // Build argv vector including program name for cxxopts
    std::vector<char*> args;
    args.push_back(argv[0]); // Program name
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    auto loadCG = [&](const std::string& cg_name) {
    };


    if (command == "reaches") {
        cxxopts::Options options("cgquery reaches", "Query reachable nodes or check if a node is reachable from a certain node");
        options.add_options()
            ("s,source", "Start node", cxxopts::value<std::string>())
            ("t,to", "Target node", cxxopts::value<std::string>())
            ("h,help", "Print help")
            ("input", "Call graph file", cxxopts::value<std::string>());

        options.parse_positional({"input"});
        options.positional_help("<input_file>");

        try {
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

 
            std::string cg_name = result["input"].as<std::string>();
            auto fs = metacg::io::FileSource(cg_name);
            auto reader = metacg::io::createReader(fs);
            auto cg = reader->read();

            auto sourceStr = result["source"].as<std::string>();
            metacg::CgNode* sourceNode = nullptr;

            if (is_number(sourceStr)) {
                sourceNode = cg->getNode(std::stoul(sourceStr));
            } else {
                if (cg->countNodes(sourceStr) > 1) {
                    metacg::MCGLogger::logWarn(
                        "To node name '" + sourceStr +
                        "' is not unique; using first matching node. "
                        "Please provide a unique node ID.");

                };
                sourceNode = cg->getFirstNode(sourceStr);
            }
            if (!sourceNode) {
                std::cerr << "Node '" << result["source"].as<std::string>() << "' not found in CG.\n";
                return 1;
            }

            metacg::analysis::ReachabilityAnalysis reachabilityAnalysis(cg.get());

            if (result.count("to")) {
                std::string toName = result["to"].as<std::string>();
                metacg::CgNode* toNode = nullptr;
                if (is_number(toName)) {
                    toNode = cg->getNode(std::stoul(toName));
                } else {
                    if (cg->countNodes(toName) > 1) {
                        metacg::MCGLogger::logWarn(
                            "To node name '" + toName +
                            "' is not unique; using first matching node. "
                            "Please provide a unique node ID.");
                    };
                    toNode = cg->getFirstNode(toName);
                }

                if (!toNode) {
                    std::cerr << "Entry node '" << toName << "' not found in CG.\n";
                    return 1;
                }

                return !reachabilityAnalysis.existsPathBetween(sourceNode, toNode, true);
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
        catch (const cxxopts::exceptions::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            std::cerr << options.help() << "\n";
            return 1;
        }
    }
    else if (command == "dom") {
        return handleDomCommand(args, "dom", "Dominator analysis", metacg::analysis::TraverseDir::Forward, "node", "entry");
    } else if (command == "postdom") {
        return handleDomCommand(args, "postdom", "Postdominator analysis", metacg::analysis::TraverseDir::Backward, "node", "exit");
    }
    else {
        std::cerr << "Unknown command " << command << "." << std::endl;
        return 2;
    }

    return 0;
}

int main(int argc, char** argv) {
    return cgQueryMain(argc, argv);
}
