#include "ReachabilityAnalysis.h"
#include "io/MCGReader.h"
#include <cxxopts.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: cgquery <command> [options] <input_file>\n";
        return 1;
    }

    std::string command = argv[1];
    std::vector<char*> args(argv + 2, argv + argc);

    std::string cg_name = argv[argc-1];

    if (command == "reaches") {
        cxxopts::Options options("cgquery reachability", "Check function reachability");
        options.add_options()
            ("f, from", "Source function", cxxopts::value<std::string>())
            ("t, to", "Target function", cxxopts::value<std::string>())
            ("i, input", "Input file", cxxopts::value<std::string>())
            ("h, help", "Print help");



        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << "\n";
            return 0;
        }

        // TODO: Check if arguments/options are missing 

        if (cg_name.empty()) {
            std::cerr << "Input file required\n";
            return 1;
        }
        if (!result.count("to")) {
            std::cerr << "Target function required. Use -t or --to to specify target function" << std::endl;

            return 1;
        }

        std::string to = result["to"].as<std::string>();

        // Read call-graph
        auto fs = metacg::io::FileSource(cg_name);
        std::unique_ptr<metacg::io::MCGReader> r1 = metacg::io::createReader(fs);
        auto cg = r1->read();


        metacg::analysis::ReachabilityAnalysis reachabilityAnalysis(cg.get());

        if (!result.count("from")) {
            return reachabilityAnalysis.existsPathBetween(cg->getMain(), cg->getFirstNode(to), true); 
        }
        else {
            std::string from = result["from"].as<std::string>();
            // NOTE: function names are not unique, should we use ids 
            return reachabilityAnalysis.existsPathBetween(cg->getFirstNode(from), cg->getFirstNode(to), true);
        }
    }
    else if (command == "stat") {
        cxxopts::Options options("cgquery stat", "return number of edges and nodes");
        options.add_options()
            ("h, help", "Print help");


        auto result = options.parse(args.size(), args.data());

        if (result.count("help")) {
            std::cout << options.help() << "\n";
            return 0;
        }

        auto fs = metacg::io::FileSource(cg_name);
        std::unique_ptr<metacg::io::MCGReader> r1 = metacg::io::createReader(fs);
        auto cg = r1->read();

        auto num_edges = cg->getEdges().size();
        auto num_nodes = cg->getNodes().size();

        std::cout << "--- Stats ---" << "\n"
            << "# of nodes: " << num_nodes << "\n" 
            << "# of edges: " << num_edges << "\n" 
            << std::endl;
        return 0;

    }
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }

    return 0;
}
