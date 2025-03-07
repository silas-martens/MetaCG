/**
 * File: cgpatch-pass.cpp
 * License: Part of the MetaCG project. Licensed under BSD 3 clause license. See LICENSE.txt file at
 * https://github.com/tudasc/metacg/LICENSE.txt
 */

#include "nlohmann/json.hpp"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <iostream>
#include <llvm/ADT/StringRef.h>
#include <string>

#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "WPA/Steensgaard.h"
#include "WPA/WPAPass.h"
#include "WPA/TypeAnalysis.h"

#include "io/VersionTwoMCGWriter.h"

#include "CallAnalysis.h"

using namespace llvm;
using namespace SVF;
using namespace metacg;
using namespace cgpatch;

static cl::opt<bool> verbose("cgpatch-verbose", cl::desc("Print debugging output"), cl::init(false));

static cl::opt<std::string> outfile("cgpatch-outfile", cl::desc("File to write the patch graph to"), cl::init("static_patch.mcg"));

namespace {

void analyzeAndEmitStaticPatchGraph(Module& M) {
  auto* SVFM = LLVMModuleSet::buildSVFModule(M);
//  SVFM->buildSymbolTableInfo();

  SVFIRBuilder builder(SVFM);
  SVFIR *pag = builder.build();


//  Steensgaard *ander = new Steensgaard(pag);
  AndersenWaveDiff* ander = new AndersenWaveDiff(pag);
//  TypeAnalysis* ander = new TypeAnalysis(pag);
  ander->analyze();

  auto* pta = ander;

  if (verbose)
    outs() << "Running CGPatch analysis during LTO\n";

  auto* moduleSet = LLVMModuleSet::getLLVMModuleSet();

  auto staticPatchCG = std::make_unique<metacg::Callgraph>();

  for (Function& F : M) {
    for (BasicBlock& B : F) {
      for (Instruction& Ins : B) {
        // Check if Ins is a call instruction
        if (auto* CB = dyn_cast<CallBase>(&Ins)) {
          auto CT = detectCallType(CB);

          if (CT == CallType::Indirect) {
            if (CB->getCalledFunction()) {
              outs() << "Function has direct target: " << *CB->getCalledFunction() << "\n";
            }

            Value* FuncPtr = CB->getCalledOperand();
            NodeID nodeId = builder.getValueNode(FuncPtr);

            if (verbose) {
              outs() << "In function " << F.getName() << "\n";
              if (CB->getCalledFunction() == nullptr) {
                outs() << "Called function is null\n";
                if (auto *alias = dyn_cast<GlobalAlias>(FuncPtr)) {
                  Function *aliasedFunction = dyn_cast<Function>(alias->getAliasee());
                  outs() << " callee is an alias of " << aliasedFunction << "\n";
                }

              }
              outs() << "Indirect call: " << *CB << "\n";
              outs() << " may target: ";
            }



            // Get the points-to set for this function pointer
            auto& pts = ander->getPts(nodeId);

            for (NodeID targetId : pts) {
              //              errs() << " -> " << targetId << ": ";
              PAGNode* pagNode = pag->getGNode(targetId);

              auto* llvmVal = moduleSet->getLLVMValue(pagNode);
              if (llvmVal) {
                auto calleeName = llvmVal->getName();
                if (isa<Function>(llvmVal)) {
                  auto* mcgNode1 = staticPatchCG->getOrInsertNode(F.getName().str());
                  auto* mcgNode2 = staticPatchCG->getOrInsertNode(calleeName.str());
                  staticPatchCG->addEdge(mcgNode1, mcgNode2);
                  if (verbose)
                    outs() << calleeName << ", ";
                }
              }
            }
            if (verbose) {
              outs() << "\nOther nodes that alias: \n";

              PAGNode* node1 = pag->getGNode(nodeId);
              ;
              for (SVFIR::iterator it = pag->begin(), elit = pag->end(); it != elit; ++it) {
                PAGNode* node2 = it->second;
                if (node1 == node2)
                  continue;
                const FunObjVar* fun1 = node1->getFunction();
                const FunObjVar* fun2 = node2->getFunction();
                AliasResult result = pta->alias(node1->getId(), node2->getId());
                if (result == AliasResult::NoAlias)
                  continue;
                SVFUtil::outs() << (result == AliasResult::MayAlias
                                        ? "MayAlias"
                                        : (result == AliasResult::MustAlias ? "MustAlias" : "PartialAlias"))
                                << " var" << node1->getId() << "[" << node1->getName() << "@"
                                << (fun1 == nullptr ? "" : fun1->getName()) << "] --"
                                << " var" << node2->getId() << "[" << node2->getName() << "@"
                                << (fun2 == nullptr ? "" : fun2->getName()) << "]\n";
              }
            }
          }
        }
      }
    }
  }
  metacg::io::VersionTwoMCGWriter mcgWriter;
  metacg::io::JsonSink jsonSink;
  mcgWriter.write(staticPatchCG.get(), jsonSink);
  nlohmann::json j = jsonSink.getJson();

  auto filename = outfile.getValue();
  std::ofstream ofs(filename);
  if (ofs.is_open()) {
    ofs << j;
    ofs.close();
  } else {
    errs() << "Unable to open file " << filename << " for writing.";
  }
}

struct CGPatchPTA : PassInfoMixin<CGPatchPTA> {
  PreservedAnalyses run(Module& M, ModuleAnalysisManager&) {
    analyzeAndEmitStaticPatchGraph(M);
    return PreservedAnalyses::all();
  }
  static bool isRequired() { return true; }
};
}  // namespace

// Registration of the new pass
llvm::PassPluginLibraryInfo getCGPatchPTAPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "cgpatch-pta", LLVM_VERSION_STRING, [](PassBuilder& PB) {
            PB.registerFullLinkTimeOptimizationEarlyEPCallback(
                [](ModulePassManager& MPM, OptimizationLevel l) { MPM.addPass(CGPatchPTA()); });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getCGPatchPTAPluginInfo();
}
