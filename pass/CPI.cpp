#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#define DEBUG_TYPE "cpi"

using namespace llvm;

struct CPI : public ModulePass {
    static char ID;
    CPI() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
        LLVMContext &ctx = M.getContext();

        // Add function references in libsafe_rt
        test = cast<Function>(M.getOrInsertFunction("test", FunctionType::getVoidTy(ctx)));

        // Loop through all functions
        for (auto &F: M.getFunctionList()) {
            // Only care locally implemented functions
            if (!F.isDeclaration())
                runOnFunction(F);
        }

        return true;
    }

    void runOnFunction(Function &F) {
        // Inject test function into main
        if (F.getName() == "main") {
            auto &BB = F.getEntryBlock();
            IRBuilder<> builder(BB.getFirstNonPHI());
            builder.CreateCall(test);
        }
    }

private:
    Function *test;
};

char CPI::ID = 0;
RegisterPass<CPI> X("cpi", "Code Pointer Integrity");

