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

enum CallType {
  Direct, Virtual, Indirect, Unknown
};

void insertMetaCGCall(Instruction& ins, Function& f, Value* calledOperand, Function* runtimeFunction);
bool isVirtualCall(const CallBase& CB);
CallType detectCallType(CallBase* Call);
Value* getTypeTestMetadata(Value *V);


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
        auto CT = detectCallType(CB);

        if(CT == CallType::Unknown)
          continue;


        auto calledFunction = CB->getCalledFunction();
        if (CT == CallType::Direct) {  // Direct call
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
          if(filterVirtualCalls && CT == CallType::Virtual)
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

CallType detectCallType(CallBase* Call) {

  // We detect the following pattern for calling virtual functions
  //   %1 = load ptr, ptr %obj -> Loading object
  //   %2 = load ptr, ptr %1   -> Loading vtable ptr from object
  //   %3 = getelementptr ptr, ptr %2, i32 index -> Getting function address location from vtable
  //   %4 = load ptr, ptr %3   -> Loading function address
  //   call void %4(ptr %obj)  -> Calling virtual function

  // Check if call is null
  if (!Call) {
    return Unknown;
  }

  // Check for direct callee
  if (Call->getCalledFunction()) {
    return Direct;
  }

  Value *FuncPtr = Call->getCalledOperand();

  LoadInst *FuncLoad = dyn_cast<LoadInst>(FuncPtr);
  if (!FuncLoad) {
    return Indirect;
  }
  Value *FuncSource = FuncLoad->getPointerOperand();
  GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(FuncSource);
  if (!GEP) {
    return Indirect;
  }
  Value *VTablePtr = GEP->getPointerOperand();

  // Get type test metadata. If we don't have this, it could just be a regular indirect call mimicking the same
  // pattern.
  Value* TypeTestMD = getTypeTestMetadata(VTablePtr);
  if (!TypeTestMD) {
    return Indirect;
  }

  LoadInst *VTableLoad = dyn_cast<LoadInst>(VTablePtr);
  if (!VTableLoad) {
    return Indirect;
  }

  Value *ObjectPtr = VTableLoad->getPointerOperand();
  Value *Idx = GEP->getOperand(1);

  // Could check if idx is 0 (meaning the VTable is the first entry in the struct), but this is not guaranteed by the standard
  //   auto *ConstIdx = dyn_cast<ConstantInt>(Idx);
  //   if (!ConstIdx || ConstIdx->isZero()) return Indirect;


  // Finally, check if first argument is 'this'
  if (Call->arg_size() > 0 && Call->getArgOperand(0) == ObjectPtr) {
    return Virtual;
  }

  // Unknown because the combination of an apparent VTable load, but not passing 'this' is very strange.
  return Unknown;
}

Value* getTypeTestMetadata(Value *V) {
  for (auto *U : V->users()) {
    //            outs() << "Testing user " << *U <<": ";
    if (auto *Intr = dyn_cast<IntrinsicInst>(U)) {
      //                outs() << " is intrisic ";
      auto ID = Intr->getIntrinsicID();
      if (ID == Intrinsic::public_type_test || ID == Intrinsic::type_test) {
        //                    outs() << " type test!\n";
        return Intr;
      }
      //                outs() << " of other type: " << Intrinsic::getName(Intr->getIntrinsicID());
    }
    //            outs() << " no\n";
  }
  return nullptr;
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
