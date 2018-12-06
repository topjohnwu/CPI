#include <llvm/Pass.h>

using namespace llvm;

struct CPI : public ModulePass {
    static char ID;
    CPI() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
      return false;
    }
};

char CPI::ID = 0;
RegisterPass<CPI> X("cpi", "Code Pointer Integrity");

