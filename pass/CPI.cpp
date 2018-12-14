#include <vector>
#include <map>
#include <algorithm>
#include <set>

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
        intT = Type::getInt32Ty(ctx);
        voidT = Type::getVoidTy(ctx);
        voidPT = Type::getInt8PtrTy(ctx);
        voidPPT = PointerType::get(voidPT, 0);

        // Add global variables in libsafe_rt
        smPool = new GlobalVariable(M, voidPPT, false, GlobalValue::ExternalLinkage, nullptr, "__sm_pool");
        smSp = new GlobalVariable(M, intT, false, GlobalValue::ExternalLinkage, nullptr, "__sm_sp");

        // Add function references in libsafe_rt
        smAlloca = cast<Function>(M.getOrInsertFunction("__sm_alloca", intT));

        // Find all sensitive structs
        for (auto s : M.getIdentifiedStructTypes()) {
            for (unsigned i = 0; i < s->getNumElements(); ++i) {
                if (isFunctionPtr(s->getElementType(i))) {
                    sensitiveStructs[s].insert(i);
                }
            }
        }

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
    Value *smPool;
    Value *smSp;

    IntegerType *intT;
    Type *voidT;
    PointerType *voidPT;
    PointerType *voidPPT;

    std::map<Type*, std::set<int> > sensitiveStructs;

    void runOnFunction(Function &F) {
        bool hasInject = false;
        for (auto &bb : F) {
            hasInject |= swapFunctionPtrAlloca(bb);
            hasInject |= swapStructAlloca(bb);
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
        auto pool = b.CreateLoad(smPool);
        auto off = b.CreateGEP(voidPT, pool, idx);
        auto cast = b.CreatePointerCast(store->getValueOperand(), voidPT);
        b.CreateStore(cast, off);
        DEBUG(dbgs() << "SWAP:" << *store << "\n");
        store->eraseFromParent();
    }

    void swapLoad(Instruction *idx, LoadInst *load) {
        IRBuilder<> b(load);
        auto pool = b.CreateLoad(smPool);
        auto off = b.CreateGEP(voidPT, pool, idx);
        auto raw = b.CreateLoad(off);
        auto cast = b.CreatePointerCast(raw, load->getType());
        DEBUG(dbgs() << "SWAP:" << *load << "\n");
        BasicBlock::iterator ii(load);
        ReplaceInstWithValue(load->getParent()->getInstList(), ii, cast);
    }

    void swapPtr(Instruction *from, Instruction *to) {
        // Swap out all uses (store and load)
        for (auto a : from->users()) {
            StoreInst *s;
            LoadInst *l;
            if ((s = dyn_cast<StoreInst>(a))) {
                swapStore(to, s);
            } else if ((l = dyn_cast<LoadInst>(a))) {
                swapLoad(to, l);
            } else {
                /* TODO: Dunno when this will happen, spit logs for now */
                DEBUG(dbgs() << "Unknown:" << *from << "\n");
            }
        }
        if (from->getNumUses() == 0) {
            DEBUG(dbgs() << "RM:" << *from << "\n");
            from->eraseFromParent();
        }
    }

    bool swapFunctionPtrAlloca(BasicBlock &bb) {
        auto v = getFunctionPtrAlloca(bb);
        for (auto alloc : v) {
            IRBuilder<> b(alloc);
            auto idx = b.CreateCall(smAlloca, None, alloc->getName());
            DEBUG(dbgs() << "ADD:" << *idx << "\n");
            swapPtr(alloc, idx);
        }
        return !v.empty();
    }

    bool swapStructAlloca(BasicBlock &bb) {
        bool hasInject = false;
        for (auto alloc : getSSAlloca(bb)) {
            std::vector<GetElementPtrInst *> rmList;
            for (auto user : alloc->users()) {
                /* Find struct entry to function pointer (2nd GEP index, or 3rd operand) */
                GetElementPtrInst *gep;
                if ((gep = dyn_cast<GetElementPtrInst>(user))) {
                    ConstantInt *ci;
                    if (gep->getNumOperands() >= 3 && (ci = dyn_cast<ConstantInt>(gep->getOperand(2)))) {
                        int entry = ci->getSExtValue();
                        if (sensitiveStructs[alloc->getAllocatedType()].count(entry)) {
                            rmList.push_back(gep);
                        }
                    }
                }
            }
            if (!rmList.empty()) {
                hasInject = true;
                IRBuilder<> b(alloc->getNextNode());
                auto idx = b.CreateCall(smAlloca);
                DEBUG(dbgs() << "ADD:" << *idx << "\n");
                for (auto u: rmList) {
                    swapPtr(u, idx);
                }
            }
        }
        return hasInject;
    }

    std::vector<AllocaInst *> getFunctionPtrAlloca(BasicBlock &bb) {
        std::vector<AllocaInst *> v;
        AllocaInst *ai;
        for (auto &I : bb) {
            if ((ai = dyn_cast<AllocaInst>(&I)) && isFunctionPtr(ai->getAllocatedType())) {
                DEBUG(dbgs() << "SENS:" << I << "\n");
                v.push_back(ai);
            }
        }
        return v;
    }

    std::vector<AllocaInst *> getSSAlloca(BasicBlock &bb) {
        std::vector<AllocaInst *> v;
        AllocaInst *ai;
        for (auto &I : bb) {
            if ((ai = dyn_cast<AllocaInst>(&I))) {
                auto i = sensitiveStructs.find(ai->getAllocatedType());
                if (i != sensitiveStructs.end()) {
                    DEBUG(dbgs() << "SENS:" << *ai << "\n");
                    v.push_back(ai);
                }
            }
        }
        return v;
    }

    bool isFunctionPtr(Type *T) {
        PointerType *t;
        return (t = dyn_cast<PointerType>(T)) && t->getElementType()->isFunctionTy();
    }
};

char CPI::ID = 0;
RegisterPass<CPI> X("cpi", "Code Pointer Integrity");

