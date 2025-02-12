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

using namespace llvm;

// option parsing
static cl::opt<bool> instrumentCtorsDtors("instrument-ctors-dtors", cl::desc("Instrument constructors and destructors"),
                                          cl::init(false));
static cl::opt<bool> filterVirtualCalls("filter-virtual-calls", cl::desc("Filter virtual calls"), cl::init(false));



namespace {
void insertMetaCGCall(Instruction& ins, Function& f, Value* calledOperand, Function* runtimeFunction);
bool isVirtualCall(const CallBase& CB);

// Instrumentation
void instrumentIndirectCalls(Module& M) {
  ItaniumPartialDemangler demangler;
  nlohmann::json j;

  // Counter variables
  int indirectCallCount{0};
  int ctorDtorCallCount{0};
  // Create function declaration
  LLVMContext& Context = M.getContext();
  auto functionType =
      FunctionType::get(Type::getVoidTy(Context), {PointerType::get(Context, 0), PointerType::get(Context, 0)}, false);
  Function* runtimeFunction = cast<Function>(M.getOrInsertFunction("__metacg_indirect_call", functionType).getCallee());

  for (Function& F : M) {
    for(BasicBlock& B : F)
      for(Instruction& Ins : B) {
        
        // Check if Ins is a call instruction
        auto* CB = dyn_cast<CallBase>(&Ins);
        if (!CB)
          continue;

        auto calledFunction = CB->getCalledFunction();
        if (calledFunction) {  // Direct call
          // Instrument constructors & destructors if option is enabled
          if (instrumentCtorsDtors) {
            // Setup demangler's internal state to work on the called function name
            demangler.partialDemangle(calledFunction->getName().str().c_str());
            if (demangler.isCtorOrDtor()) {  // constructor call
              insertMetaCGCall(Ins, F, CB->getCalledOperand(), runtimeFunction);
              ctorDtorCallCount++;
            }
          }
        } else {  // indirect call
          if(filterVirtualCalls && isVirtualCall(*CB))
                            continue;

          insertMetaCGCall(Ins, F, CB->getCalledOperand(), runtimeFunction);
          indirectCallCount++;
        }
      }
  }
  llvm::outs() << "[Info] Instrumented " << (indirectCallCount + ctorDtorCallCount) << " function calls in "
               << M.getName().str() << ":\n"
               << "\t" << indirectCallCount << ": "
               << "Indirect functions.\n"
               << "\t" << ctorDtorCallCount << ": "
               << "constructors and destructors."
               << "\n";
}

// Insert call to __metacg_indirect_call before the current instruction
void insertMetaCGCall(Instruction& ins, Function& f, Value* calledOperand, Function* runtimeFunction) {
  IRBuilder<> Builder(&ins);
  auto* strArg = Builder.CreateGlobalString(f.getName().str());
  Builder.CreateCall(runtimeFunction, {strArg, calledOperand});
}

bool isVirtualCall(const CallBase& CB) {
  if (MDNode *DevirtMetadata = CB.getMetadata("devirt")) {
    return true;
  }
  return false;
}


struct RuntimeCallInjection : PassInfoMixin<RuntimeCallInjection> {
  PreservedAnalyses run(Module& M, ModuleAnalysisManager&) {
    instrumentIndirectCalls(M);
    return PreservedAnalyses::all();
  }
  // We also need to be able to instrument optnone annotated functions
  static bool isRequired() { return true; }
};
}  // namespace

// Registration of the new pass
llvm::PassPluginLibraryInfo getRuntimeCallInjectionPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "runtime-call-injection", LLVM_VERSION_STRING, [](PassBuilder& PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager& MPM, OptimizationLevel l) { MPM.addPass(RuntimeCallInjection()); });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getRuntimeCallInjectionPluginInfo();
}
