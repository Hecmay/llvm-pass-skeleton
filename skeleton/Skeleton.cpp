#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h" 
#include "llvm/IR/Dominators.h"

#include <assert.h>
using namespace llvm;

namespace {
  struct SkeletonPass : public FunctionPass {
    static char ID;
    SkeletonPass() : FunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      AU.addRequired<LoopInfoWrapperPass>();
    }

    virtual bool runOnFunction(Function &F) {
      // craete llvm function with
      LLVMContext &Ctx = F.getContext();
      std::vector<Type*> paramTypes = {Type::getInt32Ty(Ctx)};
      Type *retType = Type::getVoidTy(Ctx);
      FunctionType *logFuncType = FunctionType::get(retType, paramTypes, false);
      // get module out of function  
      Module* module = F.getParent();
      Constant *logFunc = module->getOrInsertFunction("logop", logFuncType);

      // analyze the loops 
      bool mutate = false;
      LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
      DominatorTree DT(F); LI.analyze(DT);

      // process each loop
      for (auto &B : F) {
        BasicBlock* b = &B;
        auto loop = LI.getLoopFor(b);
        if (L.contain(B)) void;
        if (loop != nullptr) {
          auto k = loop->getHeader()->getName();
          errs() << k << ":" << B.getName() << "\n";
        }
        // errs() << "analyze bb: " << B.getName() << "\n";
        // analyze basic blocks with loops
        if (auto bi = dyn_cast<BranchInst>(B.getTerminator())) {
          // Value *loopCond = bi->getCondition();
          for (auto &I : B) {
            if(isa<CallInst>(&I) || isa<InvokeInst>(&I)){
              errs() << cast<CallInst>(&I)->getCalledFunction()->getName() << "\t";
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

    // dump the mutated llvm ir & execute 
    std::error_code ecode;
    raw_fd_ostream dest("opt.ll", ecode);
    module->print(dest, nullptr);
    return true;
    }
  };
}

char SkeletonPass::ID = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerSkeletonPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new SkeletonPass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                 registerSkeletonPass);
