#include <vector>
#include <map>

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
        smSp = new GlobalVariable(M, intT, false, GlobalValue::ExternalLinkage, nullptr, "__sm_sp");

        // Add function references in libsafe_rt
        smAlloca = cast<Function>(M.getOrInsertFunction("__sm_alloca", voidPPT));

        // Find all sensitive structs
        for (auto s : M.getIdentifiedStructTypes()) {
            for (unsigned i = 0; i < s->getNumElements(); ++i) {
                if (isFunctionPtr(s->getElementType(i))) {
                    ssMap[s].push_back(i);
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
    Value *smSp;

    IntegerType *intT;
    Type *voidT;
    PointerType *voidPT;
    PointerType *voidPPT;

    // A map of StructType to the list of entries numbers that are function pointers
    std::map<Type*, std::vector<int> > ssMap;

    void runOnFunction(Function &F) {
        bool hasInject = false;

        BasicBlock &entryBlock = F.getEntryBlock();

        // Alloca only happens in the first basic block
        hasInject |= swapFunctionPtrAlloca(entryBlock);
        hasInject |= handleStructAlloca(entryBlock);

        if (hasInject) {
            // Create checkpoint
            auto spLoad = new LoadInst(smSp, "smStackCheckpoint", entryBlock.getFirstNonPHI());
            for (auto &bb : F) {
                auto ti = bb.getTerminator();
                if (isa<ReturnInst>(ti)) {
                    // Restore checkpoint
                    new StoreInst(spLoad, smSp, ti);
                }
            }
        }
    }

    void swapStore(Instruction *addr, StoreInst *store) {
        IRBuilder<> b(store);
        auto cast = b.CreatePointerCast(store->getValueOperand(), voidPT);
        b.CreateStore(cast, addr);
        DEBUG(dbgs() << "SWAP:" << *store << "\n");
        store->eraseFromParent();
    }

    void swapLoad(Instruction *addr, LoadInst *load) {
        IRBuilder<> b(load);
        auto raw = b.CreateLoad(addr);
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
                DEBUG(dbgs() << "OTHER:" << *from << "\n");
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
            std::string name(alloc->getName());
            auto addr = b.CreateCall(smAlloca);
            DEBUG(dbgs() << "ADD:" << *addr << "\n");
            swapPtr(alloc, addr);
            addr->setName(name);
        }
        return !v.empty();
    }

    bool handleStructAlloca(BasicBlock &bb) {
        bool hasInject = false;
        for (auto alloc: getSSAlloca(bb)) {
            for (int fpentry : ssMap[alloc->getAllocatedType()]) {
                std::vector<GetElementPtrInst *> rmList;
                for (auto user : alloc->users()) {
                    auto *gep = dyn_cast<GetElementPtrInst>(user);
                    if (isSensitiveGEP(gep, fpentry))
                        rmList.push_back(gep);
                }
                if (rmList.empty())
                    continue;

                hasInject = true;
                IRBuilder<> b(alloc->getNextNode());
                auto addr = b.CreateCall(smAlloca, None, alloc->getName() + "." + std::to_string(fpentry));
                DEBUG(dbgs() << "ADD:" << *addr << "\n");

                // Check for external calls
                GetElementPtrInst *gep = nullptr;
                for (auto user : alloc->users()) {
                    CallInst *ci;
                    if ((ci = dyn_cast<CallInst>(user))) {
                        if (!gep) {
                            gep = cast<GetElementPtrInst>(rmList.front()->clone());
                            gep->setName(addr->getName() + ".orig");
                            gep->insertAfter(addr);
                        }
                        commitAndRestore(addr, voidPT, gep, gep->getResultElementType(), ci);
                    }
                }

                for (auto u: rmList) {
                    swapPtr(u, addr);
                }
            }
        }
        return hasInject;
    }

    void commitAndRestore(Value *a, Type *aType, Value *b, Type *bType, Instruction *i) {
        // Commit sm memory to actual memory
        IRBuilder<> builder(i);
        auto v = builder.CreateLoad(a);
        auto cast = builder.CreatePointerCast(v, bType);
        builder.CreateStore(cast, b);
        // Restore actual memory back to sm memory
        builder.SetInsertPoint(i->getNextNode());
        v = builder.CreateLoad(b);
        cast = builder.CreatePointerCast(v, aType);
        builder.CreateStore(cast, a);
    }

    std::vector<AllocaInst *> getSensitiveAlloca(BasicBlock &bb, const std::function<bool(AllocaInst *)> &filter) {
        std::vector<AllocaInst *> v;
        AllocaInst *ai;
        for (auto &I : bb) {
            if ((ai = dyn_cast<AllocaInst>(&I)) && filter(ai)) {
                DEBUG(dbgs() << "SENS:" << I << "\n");
                v.push_back(ai);
            }
        }
        return v;
    }

    std::vector<AllocaInst *> getFunctionPtrAlloca(BasicBlock &bb) {
        return getSensitiveAlloca(bb, [this](auto i) -> bool {
            if (!isFunctionPtr(i->getAllocatedType()))
                return false;
            for (auto user : i->users()) {
                // If the pointer is passed to a function call, skip it
                if (isa<CallInst>(user))
                    return false;
            }
            return true;
        });
    }

    std::vector<AllocaInst *> getSSAlloca(BasicBlock &bb) {
        return getSensitiveAlloca(bb, [this](auto i) -> bool {
            return ssMap.count(i->getAllocatedType());
        });
    }

    /* Check struct entry to function pointer (2nd GEP index, or 3rd operand) */
    bool isSensitiveGEP(GetElementPtrInst *gep, int fpentry) {
        if (gep == nullptr)
            return false;
        ConstantInt *ci;
        return gep->getNumOperands() >= 3 &&
        (ci = dyn_cast<ConstantInt>(gep->getOperand(2))) &&
        ci->getSExtValue() == fpentry;
    }

    bool isFunctionPtr(Type *T) {
        PointerType *t;
        return (t = dyn_cast<PointerType>(T)) && t->getElementType()->isFunctionTy();
    }
};

char CPI::ID = 0;
RegisterPass<CPI> X("cpi", "Code Pointer Integrity");

