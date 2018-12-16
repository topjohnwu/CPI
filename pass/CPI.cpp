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
        smMalloc = cast<Function>(M.getOrInsertFunction("__sm_malloc", voidPPT, voidPPT));
        smLoad = cast<Function>(M.getOrInsertFunction("__sm_load", voidPT, voidPPT, voidPPT));

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
    Function *smMalloc;
    Function *smLoad;
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
        hasInject |= handleStructPointers(F);

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
            hasInject |= handleSSFPEntries(alloc, alloc->getNextNode());
        }
        return hasInject;
    }

    bool handleStructPointers(Function &F) {
        bool hasInject = false;
        auto fi = F.getEntryBlock().getFirstNonPHI();
        for (auto &arg : F.args()) {
            if (isSSPtr(arg.getType())) {
                hasInject |= handleUSSFPEntries(&arg, fi);
            }
        }
        return hasInject;
    }

    bool handleUSSFPEntries(Value *ssp, Instruction *insert) {
        std::map<std::pair<int, int>, std::vector<GetElementPtrInst *> > rmMap;
        for (int sentry : ssMap[cast<PointerType>(ssp->getType())->getElementType()]) {
            for (auto user : ssp->users()) {
                int idx;
                auto *gep = dyn_cast<GetElementPtrInst>(user);
                if ((idx = isSensitiveGEP(gep, sentry)) >= 0) {
                    rmMap[{idx, sentry}].push_back(gep);
                }
            }
        }
        if (rmMap.empty())
            return false;

        for (const auto &geps : rmMap) {
            std::string name(ssp->getName());
            name += "." + std::to_string(geps.first.first) + "." + std::to_string(geps.first.second);

            IRBuilder<> b(insert);
            auto tmp = b.CreateGEP(ssp,
                    {ConstantInt::get(intT, geps.first.first), ConstantInt::get(intT, geps.first.second)});
            auto orig = b.CreatePointerCast(tmp, voidPPT, name + ".orig");
            auto addr = b.CreateCall(smMalloc, orig, name);

            DEBUG(dbgs() << "ADD:" << *addr << "\n");

            for (auto u: geps.second) {
                uSwapPtr(u, addr, orig);
            }

            // Check for external calls
            for (auto user : ssp->users()) {
                CallInst *ci;
                if ((ci = dyn_cast<CallInst>(user))) {
                    auto load = new LoadInst(orig);
                    load->insertAfter(ci);
                    (new StoreInst(load, addr))->insertAfter(load);
                }
            }

        }

        return true;
    }

    bool handleSSFPEntries(Value *ssp, Instruction *insert) {
        std::map<std::pair<int, int>, std::vector<GetElementPtrInst *> > rmMap;
        for (int sentry : ssMap[cast<PointerType>(ssp->getType())->getElementType()]) {
            for (auto user : ssp->users()) {
                int idx;
                auto *gep = dyn_cast<GetElementPtrInst>(user);
                if ((idx = isSensitiveGEP(gep, sentry)) >= 0) {
                    rmMap[{idx, sentry}].push_back(gep);
                }
            }
        }
        if (rmMap.empty())
            return false;

        for (const auto &geps : rmMap) {
            IRBuilder<> b(insert);
            auto addr = b.CreateCall(smAlloca, None,
                    ssp->getName() + "." + std::to_string(geps.first.first) + "." + std::to_string(geps.first.second));
            DEBUG(dbgs() << "ADD:" << *addr << "\n");
            GetElementPtrInst *orig = GetElementPtrInst::Create(nullptr, ssp,
                    {ConstantInt::get(intT, geps.first.first), ConstantInt::get(intT, geps.first.second)},
                    addr->getName() + ".orig", addr->getNextNode());

            for (auto u: geps.second) {
                swapPtr(u, addr);
            }

            // Check for external calls
            for (auto user : ssp->users()) {
                CallInst *ci;
                if ((ci = dyn_cast<CallInst>(user))) {
                    commitAndRestore(addr, voidPT, orig, orig->getResultElementType(), ci);
                }
            }

            // Remove if not needed
            if (orig->getNumUses() == 0)
                orig->eraseFromParent();
        }

        return true;
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

    void uSwapStore(Instruction *addr, StoreInst *store, Value *orig) {
        IRBuilder<> b(store);
        auto cast = b.CreatePointerCast(store->getValueOperand(), voidPT);
        b.CreateStore(cast, addr);
        b.CreateStore(cast, orig);
        DEBUG(dbgs() << "ADD:" << *store << "\n");
        store->eraseFromParent();
    }

    void uSwapLoad(Instruction *addr, LoadInst *load, Value *orig) {
        IRBuilder<> b(load);
        auto raw = b.CreateCall(smLoad, {addr, orig});
        auto cast = b.CreatePointerCast(raw, load->getType());
        DEBUG(dbgs() << "SWAP:" << *load << "\n");
        BasicBlock::iterator ii(load);
        ReplaceInstWithValue(load->getParent()->getInstList(), ii, cast);
    }

    void uSwapPtr(Instruction *from, Instruction *to, Value *orig) {
        // Swap out all uses (store and load)
        for (auto a : from->users()) {
            StoreInst *s;
            LoadInst *l;
            if ((s = dyn_cast<StoreInst>(a))) {
                uSwapStore(to, s, orig);
            } else if ((l = dyn_cast<LoadInst>(a))) {
                uSwapLoad(to, l, orig);
            } else {
                DEBUG(dbgs() << "OTHER:" << *from << "\n");
            }
        }
        if (from->getNumUses() == 0) {
            DEBUG(dbgs() << "RM:" << *from << "\n");
            from->eraseFromParent();
        }
    }

    // Commit sm memory to actual memory
    void commit(Value *a, Value *b, Type *bType, Instruction *i) {
        IRBuilder<> builder(i);
        auto v = builder.CreateLoad(a);
        auto cast = builder.CreatePointerCast(v, bType);
        builder.CreateStore(cast, b);
    }

    void restore(Value *a, Type *aType, Value *b, Instruction *i) {
        IRBuilder<> builder(i);
        auto v = builder.CreateLoad(b);
        auto cast = builder.CreatePointerCast(v, aType);
        builder.CreateStore(cast, a);
    }

    void commitAndRestore(Value *a, Type *aType, Value *b, Type *bType, Instruction *i) {
        commit(a, b, bType, i);
        restore(a, aType, b, i->getNextNode());
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
    int isSensitiveGEP(GetElementPtrInst *gep, int fpentry) {
        ConstantInt *ci;
        if(gep && gep->getNumOperands() >= 3 &&
            (ci = dyn_cast<ConstantInt>(gep->getOperand(2))) &&
            ci->getSExtValue() == fpentry &&
            (ci = dyn_cast<ConstantInt>(gep->getOperand(1)))) {
            return ci->getSExtValue();
        }
        return -1;
    }

    bool isFunctionPtr(Type *T) {
        PointerType *t;
        return (t = dyn_cast<PointerType>(T)) && t->getElementType()->isFunctionTy();
    }

    bool isSSPtr(Type *T) {
        PointerType *t;
        if (!(t = dyn_cast<PointerType>(T)))
            return false;
        for (const auto &p : ssMap) {
            if (t->getElementType() == p.first)
                return true;
        }
        return false;
    }
};

char CPI::ID = 0;
RegisterPass<CPI> X("cpi", "Code Pointer Integrity");

