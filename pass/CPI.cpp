#include <vector>

#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "cpi"

using namespace llvm;

struct CPI : public ModulePass {
    static char ID;
    CPI() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        LLVMContext &ctx = M.getContext();

        // Commonly used types
        Type *intT = Type::getInt32Ty(ctx);
        voidT = Type::getVoidTy(ctx);
        voidPT = Type::getInt8PtrTy(ctx);
        voidPPT = PointerType::get(voidPT, 0);

        smPool = new GlobalVariable(M, voidPPT, false, GlobalValue::ExternalLinkage, nullptr, "__sm_pool");
        smSp = new GlobalVariable(M, intT, false, GlobalValue::ExternalLinkage, nullptr, "__sm_sp");

        // Add function references in libsafe_rt
        smAlloca = cast<Function>(M.getOrInsertFunction("smAlloca", intT));
        smStore = cast<Function>(M.getOrInsertFunction("smStore", voidT, intT, voidPT));
        smLoad = cast<Function>(M.getOrInsertFunction("smLoad", voidPT, intT));
        smDeref = cast<Function>(M.getOrInsertFunction("smDeref", voidPPT, intT));

        // Loop through all functions
        for (auto &F: M.getFunctionList()) {
            // Only care locally implemented functions
            if (!F.isDeclaration())
                runOnFunction(F);
        }

        return true;
    }

private:
    Function *smAlloca;
    Function *smStore;
    Function *smLoad;
    Function *smDeref;  /* Temporarily unused */
    Value *smPool;
    Value *smSp;

    Type *voidT;
    PointerType *voidPT;
    PointerType *voidPPT;

    void runOnFunction(Function &F) {
        bool hasInject = false;
        for (auto &bb : F) {
            auto v = getCPSPtrs(bb);
            hasInject |= !v.empty();
            for (auto I : v) {
                IRBuilder<> b(I);

                // Get index from smAlloca
                auto idx = b.CreateCall(smAlloca, None, I->getName());

                // Swap out all uses (store and load)
                for (auto a : I->users()) {
                    StoreInst *s;
                    LoadInst *l;
                    if ((s = dyn_cast<StoreInst>(a))) {
                        swapStore(idx, s);
                    } else if ((l = dyn_cast<LoadInst>(a))) {
                        swapLoad(idx, l);
                    } else {
                        /* TODO: Dunno when this will happen, spit logs for now */
                        DEBUG(dbgs() << *I << "\n");
                    }
                }
                if (I->getNumUses() == 0)
                    I->eraseFromParent();
            }
        }

        // Stack maintenance
        if (hasInject) {
            auto fi = F.front().getFirstNonPHI();
            auto spLoad = new LoadInst(smSp, "", fi);
            for (auto &bb : F) {
                auto ti = bb.getTerminator();
                if (isa<ReturnInst>(ti)) {
                    new StoreInst(spLoad, smSp, ti);
                }
            }
        }
    }

    void swapStore(Instruction *idx, StoreInst *store) {
        IRBuilder<> b(store);
        auto cast = b.CreatePointerCast(store->getValueOperand(), voidPT);
        b.CreateCall(smStore, {idx, cast});
        store->eraseFromParent();
    }

    void swapLoad(Instruction *idx, LoadInst *load) {
        IRBuilder<> b(load);
        auto raw = b.CreateCall(smLoad, idx);
        auto cast = b.CreatePointerCast(raw, load->getType());
        BasicBlock::iterator ii(load);
        ReplaceInstWithValue(load->getParent()->getInstList(), ii, cast);
    }

    /* TODO: Might need better CPS pointer detection
     * Currently only covers function pointer alloca */
    std::vector<Instruction *> getCPSPtrs(BasicBlock &bb) {
        std::vector<Instruction *> v;
        for (Instruction &I : bb) {
            if (isAllocaFunctionPtr(I)) {
                DEBUG(dbgs() << "CPS: " << I << "\n");
                v.push_back(&I);
            }
        }
        return v;
    }

    bool isAllocaFunctionPtr(Instruction &I) {
        AllocaInst *i;
        return (i = dyn_cast<AllocaInst>(&I)) && isFunctionPtr(i->getAllocatedType());
    }

    bool isFunctionPtr(Type *T) {
        PointerType *t;
        return (t = dyn_cast<PointerType>(T)) && t->getElementType()->isFunctionTy();
    }
};

char CPI::ID = 0;
RegisterPass<CPI> X("cpi", "Code Pointer Integrity");

