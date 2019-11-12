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
      for(auto* L : LI){
        PHINode *Node = L->getCanonicalInductionVariable();
        if (Node != nullptr) {
          errs() << Node->getName() <<"phi: \n";
          // auto workList = L->getBlocksSet();
          // SmallVector<Instruction *, 16> WorkListVec;
          // for (Instruction &I : instructions(*loop)) {
          //   WorkListVec.push_back(&I);
          // }
        }        

        bool mutate = false;
        for (auto &B : F) {
          BasicBlock* b = &B;
          // analyze basic blocks with loops
          if (auto bi = dyn_cast<BranchInst>(B.getTerminator())) {
            // Value *loopCond = bi->getCondition();
            for (auto &I : B) {
              if(isa<CallInst>(&I) || isa<InvokeInst>(&I)){
                errs() << cast<CallInst>(&I)->getCalledFunction()->getName() << "\n";
              } else if (isa<PHINode>(&I)) {
                errs() << "phi:" << I; 
              }
              if (auto *op = dyn_cast<BinaryOperator>(&I)) {

                IRBuilder<> builder(op); // ir builder at op
                Value *lhs = op->getOperand(0);
                Value *rhs = op->getOperand(1);
                Value *mul = builder.CreateMul(lhs, rhs);
  
                mutate = true;
                for (auto &U : op->uses()) {
                  User *user = U.getUser();  
                  // user->setOperand(U.getOperandNo(), mul);
                }
              }
            }
          }
        }
      }
      return true;
    }
  };
}

char SkeletonPass::ID = 0;
static RegisterPass<SkeletonPass> X("mypass", "Stregth Reduction Pass",
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
