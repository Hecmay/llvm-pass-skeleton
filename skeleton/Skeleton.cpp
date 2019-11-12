#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/LoopInfo.h" 
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Scalar.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include <assert.h>
using namespace llvm;

namespace {
  struct SkeletonPass : public FunctionPass {
    static char ID;
    SkeletonPass() : FunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addRequired<TargetLibraryInfoWrapperPass>();
    }

    virtual bool runOnFunction(Function &F) {
      // craete llvm function with
      LLVMContext &Ctx = F.getContext();
      std::vector<Type*> paramTypes = {Type::getInt32Ty(Ctx)};
      Type *retType = Type::getVoidTy(Ctx);
      FunctionType *logFuncType = FunctionType::get(retType, paramTypes, false);
      Module* module = F.getParent();
      Constant *logFunc = module->getOrInsertFunction("logop", logFuncType);

      // perform constant prop and loop analysis
      // should not call other passes with runOnFunction
      // which may overwrite the original pass manager
      LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

      // induction var canonicalization  
      legacy::FunctionPassManager FPM(module);
      FPM.add(createConstantPropagationPass());
      FPM.add(createLoopSimplifyPass());
      FPM.add(createIndVarSimplifyPass());
      FPM.add(createDeadCodeEliminationPass());
      FPM.doInitialization();
      bool changed = FPM.run(F);
      FPM.doFinalization();

      // induction var reduction
      for(auto* L : LI) {
        // initalize worklist to find indvar
        SmallPtrSet<Value*, 16> IndVarList;
        for (auto &B : F) 
          for (auto &I : B) 
            if (PHINode *PN = dyn_cast<PHINode>(&I)) 
              IndVarList.insert(&I);
        // find all indvar
        while (true) {
          SmallPtrSet<Value*, 16> NewList = IndVarList;
          for (auto &B : F) {
            BasicBlock* b = &B;
            if (LI.getLoopFor(b) == L) {
              for (auto &I : B) {
                if (auto *op = dyn_cast<BinaryOperator>(&I)) {
                  Value *lhs = op->getOperand(0);
                  Value *rhs = op->getOperand(1);
                  if (IndVarList.count(lhs) || IndVarList.count(rhs)) 
                    NewList.insert(&I); 
                }
              }
            } // finish block traversal
          } // finish loop traversal
          if (NewList.size() == IndVarList.size()) break;
          else IndVarList = NewList;
        }
      } // finish walking basic blocks in loop
      return true;
    } // finish processing loops
  };
}

char SkeletonPass::ID = 0;
static RegisterPass<SkeletonPass> X("sr", "Stregth Reduction Pass",
                                    false /* Only looks at CFG */,
                                    true /* Not Analysis Pass */);

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerSkeletonPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new SkeletonPass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_LoopOptimizerEnd,
                 registerSkeletonPass);
