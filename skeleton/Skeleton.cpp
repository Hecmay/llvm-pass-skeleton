#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/LoopInfo.h" 
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

#include <tuple>
#include <map>
using namespace std;

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

      // apply useful passes
      legacy::FunctionPassManager FPM(module);
      FPM.add(createConstantPropagationPass());
      FPM.add(createIndVarSimplifyPass());
      FPM.add(createDeadCodeEliminationPass());
      FPM.doInitialization();
      bool changed = FPM.run(F);
      FPM.doFinalization();

      // find all loop induction variables within a loop
      for(auto* L : LI) {
        // IndVarMap = {indvar: indvar tuple}
        // indvar tuple = (basic_indvar, scale, const)
        // indvar = basic_indvar * scale + const
        map<Value*, tuple<Value*, int, int> > IndVarMap;
        // collect all basic indvars by visiting all phi nodes
        for (auto &B : F) {
          for (auto &I : B) {
            if (PHINode *PN = dyn_cast<PHINode>(&I)) {
              IndVarMap[&I] = make_tuple(&I, 1, 0);
            }
          }
        }
        // find all indvars
        // keep modifying the set until the size does not change
        while (true) {
          map<Value*, tuple<Value*, int, int> > NewMap = IndVarMap;
          for (auto &B : F) {
            // if the basic block is inside the target loop L
            // iterate through all its instructions
            if (LI.getLoopFor(&B) == L) {
              for (auto &I : B) {
                // we only accept multiplication, addition, and subtraction
                // we only accept constant integer as one of theoperands
                if (auto *op = dyn_cast<BinaryOperator>(&I)) {
                  Value *lhs = op->getOperand(0);
                  Value *rhs = op->getOperand(1);
                  // check if one of the operands belongs to indvars
                  if (IndVarMap.count(lhs) || IndVarMap.count(rhs)) {
                    // case: Add
                    if (I.getOpcode() == Instruction::Add) {
                      ConstantInt* CIL = dyn_cast<ConstantInt>(lhs);
                      ConstantInt* CIR = dyn_cast<ConstantInt>(rhs);
                      if (IndVarMap.count(lhs) && CIR) {
                        tuple<Value*, int, int> t = IndVarMap[lhs];
                        int new_val = CIR->getSExtValue() + get<2>(t);
                        NewMap[&I] = make_tuple(get<0>(t), get<1>(t), new_val);
                      } else if (IndVarMap.count(rhs) && CIL) {
                        tuple<Value*, int, int> t = IndVarMap[rhs];
                        int new_val = CIL->getSExtValue() + get<2>(t);
                        NewMap[&I] = make_tuple(get<0>(t), get<1>(t), new_val);
                      }
                    // case: Sub
                    } else if (I.getOpcode() == Instruction::Sub) {
                      ConstantInt* CIL = dyn_cast<ConstantInt>(lhs);
                      ConstantInt* CIR = dyn_cast<ConstantInt>(rhs);
                      if (IndVarMap.count(lhs) && CIR) {
                        tuple<Value*, int, int> t = IndVarMap[lhs];
                        int new_val = get<2>(t) - CIR->getSExtValue();
                        NewMap[&I] = make_tuple(get<0>(t), get<1>(t), new_val);
                      } else if (IndVarMap.count(rhs) && CIL) {
                        tuple<Value*, int, int> t = IndVarMap[rhs];
                        int new_val = get<2>(t) - CIL->getSExtValue();
                        NewMap[&I] = make_tuple(get<0>(t), get<1>(t), new_val);
                      }
                    // case: Mul
                    } else if (I.getOpcode() == Instruction::Mul) {
                      ConstantInt* CIL = dyn_cast<ConstantInt>(lhs);
                      ConstantInt* CIR = dyn_cast<ConstantInt>(rhs);
                      if (IndVarMap.count(lhs) && CIR) {
                        tuple<Value*, int, int> t = IndVarMap[lhs];
                        int new_val = CIR->getSExtValue() * get<1>(t);
                        NewMap[&I] = make_tuple(get<0>(t), new_val, get<2>(t));
                      } else if (IndVarMap.count(rhs) && CIL) {
                        tuple<Value*, int, int> t = IndVarMap[rhs];
                        int new_val = CIL->getSExtValue() * get<1>(t);
                        NewMap[&I] = make_tuple(get<0>(t), new_val, get<2>(t));
                      }
                    }
                  } // if operand in indvar
                } // if op is binop
              } // if B inside L
            } // auto &I: B
          } // auto &B: F
          if (NewMap.size() == IndVarMap.size()) break;
          else IndVarMap = NewMap;
        }

        // now modify the loop to apply strength reduction
        map<Value*, PHINode*> PhiMap;
        // note that after loop simplification
        // we will only have a unique header and preheader
        //
        // the preheader block
        BasicBlock* b_preheader = L->getLoopPreheader();
        // the header block
        BasicBlock* b_header = L->getHeader();
        // the body block
        BasicBlock* b_body;

        // modify the preheader block by inserting new phi nodes
        Value* preheader_val;
        for (auto &B : F) {
          if (&B == b_header) {
            for (auto &I : B) {
              // we assume we only have a single phi node
              if (PHINode *PN = dyn_cast<PHINode>(&I)) {
                int num_income = PN->getNumIncomingValues();
                // find the preheader value of the phi node
                for (int i = 0; i < num_income; i++) {
                  if (PN->getIncomingBlock(i) == b_preheader) {
                    preheader_val = PN->getIncomingValue(i);
                  } else {
                    b_body = PN->getIncomingBlock(i);
                  }
                }
                IRBuilder<> head_builder(&I);
                // create a new phi-node for replacement
                for (auto &indvar : IndVarMap) {
                  tuple<Value*, int, int> t = indvar.second;
                  if (get<1>(t) != 1 || get<2>(t) != 0) { // not a basic indvar
                    // calculate the new indvar according to the preheader value
                    Value* new_incoming = head_builder.CreateMul(preheader_val, ConstantInt::getSigned(preheader_val->getType(), get<1>(t)));
                    new_incoming = head_builder.CreateAdd(new_incoming, ConstantInt::getSigned(preheader_val->getType(), get<2>(t)));
                    PHINode* new_phi = head_builder.CreatePHI(preheader_val->getType(), 2);
                    new_phi->addIncoming(new_incoming, b_preheader);
                    PhiMap[indvar.first] = new_phi;
                  }
                }
              }
            }
          }
        }

        // modify the new body block by inserting cheaper computation
        for (auto &B : F) {
          if (&B == b_body) {
            for (auto &indvar : IndVarMap) {
              tuple<Value*, int, int> t = indvar.second;
              if (get<1>(t) != 1 || get<2>(t) != 0) { // not a basic indvar
                for (auto &I : B) {
                  if (auto op = dyn_cast<BinaryOperator>(&I)) {
                    Value *lhs = op->getOperand(0);
                    Value *rhs = op->getOperand(1);
                    if (lhs == get<0>(t) || rhs == get<0>(t)) {
                      IRBuilder<> body_builder(&I);
                      tuple<Value*, int, int> t_basic = IndVarMap[&I];
                      int new_val = get<1>(t) * get<2>(t_basic);
                      PHINode* phi_val = PhiMap[indvar.first];
                      Value* new_incoming = body_builder.CreateAdd(phi_val, ConstantInt::getSigned(phi_val->getType(), new_val));
                      phi_val->addIncoming(new_incoming, b_body);
                    }
                  }
                }
              }
            }
          }
        }

        // replace all the original uses with phi-node
        for (auto &B : F) {
          for (auto &I : B) {
            if (PhiMap.count(&I)) {
              I.replaceAllUsesWith(PhiMap[&I]);
            }
          }
        }

      } // finish all loops

      // do another round of optimization
      FPM.doInitialization();
      changed = FPM.run(F);
      FPM.doFinalization();

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
