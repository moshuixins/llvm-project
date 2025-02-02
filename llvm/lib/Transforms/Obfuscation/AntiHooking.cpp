/*
    LLVM Anti Hooking Pass
    Copyright (C) 2017 Zhang(https://github.com/Naville/)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "llvm/ADT/Triple.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Obfuscation/compat/CallSite.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

static cl::opt<string>
    PreCompiledIRPath("adhexrirpath",
                      cl::desc("External Path Pointing To Pre-compiled Anti "
                               "Hooking Handler IR.See Wiki"),
                      cl::value_desc("filename"), cl::init(""));
static cl::opt<bool> AntiRebindSymbol("ah_antirebind",
                                      cl::desc("Make fishhook unavailable"),
                                      cl::value_desc("unavailable fishhook"),
                                      cl::init(false), cl::Optional);

using namespace llvm;
using namespace std;

namespace llvm {
struct AntiHook : public ModulePass {
  static char ID;
  bool flag;
  bool appleptrauth;
  bool opaquepointers;
  bool hasobjcmethod;
  Triple triple;
  map<string, GlobalVariable *> funcname2gv;
  AntiHook() : ModulePass(ID) {
    this->flag = true;
    this->hasobjcmethod = false;
  }
  AntiHook(bool flag) : ModulePass(ID) {
    this->flag = flag;
    this->hasobjcmethod = false;
  }
  StringRef getPassName() const override { return "AntiHook"; }
  bool doInitialization(Module &M) override {
    if (PreCompiledIRPath == "") {
      SmallString<32> Path;
      if (sys::path::home_directory(Path)) { // Stolen from LineEditor.cpp
        sys::path::append(Path, "Hikari");
        sys::path::append(Path,
                          "PrecompiledAntiHooking-" +
                              Triple::getArchTypeName(triple.getArch()) + "-" +
                              Triple::getOSTypeName(triple.getOS()) + ".bc");
        PreCompiledIRPath = Path.c_str();
      }
    }
    ifstream f(PreCompiledIRPath);
    if (f.good()) {
      errs() << "Linking PreCompiled AntiHooking IR From:" << PreCompiledIRPath
             << "\n";
      SMDiagnostic SMD;
      unique_ptr<Module> ADBM(
          parseIRFile(StringRef(PreCompiledIRPath), SMD, M.getContext()));
      Linker::linkModules(M, std::move(ADBM), Linker::Flags::OverrideFromSrc);
    } else {
      errs() << "Failed To Link PreCompiled AntiHooking IR From:"
             << PreCompiledIRPath << "\n";
    }
    opaquepointers = !M.getContext().supportsTypedPointers();
    appleptrauth = hasApplePtrauth(&M);
    triple = Triple(M.getTargetTriple());
    if (triple.getVendor() == Triple::VendorType::Apple) {
      for (GlobalVariable &GV : M.globals()) {
        if (GV.hasName() && GV.hasInitializer() &&
            (GV.getName().startswith("_OBJC_$_INSTANCE_METHODS") ||
             GV.getName().startswith("_OBJC_$_CLASS_METHODS"))) {
          hasobjcmethod = true;
          break;
        }
      }
      if (hasobjcmethod) {
        Type *Int8PtrTy = Type::getInt8PtrTy(M.getContext());
        M.getOrInsertFunction("objc_getClass",
                              FunctionType::get(Int8PtrTy, {Int8PtrTy}, false));
        M.getOrInsertFunction("sel_registerName",
                              FunctionType::get(Int8PtrTy, {Int8PtrTy}, false));
        FunctionType *IMPType =
            FunctionType::get(Int8PtrTy, {Int8PtrTy, Int8PtrTy}, true);
        PointerType *IMPPointerType = PointerType::getUnqual(IMPType);
        M.getOrInsertFunction(
            "method_getImplementation",
            FunctionType::get(IMPPointerType,
                              {PointerType::getUnqual(StructType::getTypeByName(
                                  M.getContext(), "struct._objc_method"))},
                              false));
        M.getOrInsertFunction(
            "class_getInstanceMethod",
            FunctionType::get(PointerType::getUnqual(StructType::getTypeByName(
                                  M.getContext(), "struct._objc_method")),
                              {Int8PtrTy, Int8PtrTy}, false));
        M.getOrInsertFunction(
            "class_getClassMethod",
            FunctionType::get(PointerType::getUnqual(StructType::getTypeByName(
                                  M.getContext(), "struct._objc_method")),
                              {Int8PtrTy, Int8PtrTy}, false));
      }
    }
    return true;
  }
  bool runOnModule(Module &M) override {
    for (Function &F : M) {
      if (toObfuscate(flag, &F, "antihook") && !F.isPresplitCoroutine() &&
          !F.hasFnAttribute(Attribute::AttrKind::AlwaysInline)) {
        errs() << "Running AntiHooking On " << F.getName() << "\n";
        if (F.hasFnAttribute(Attribute::AttrKind::MinSize))
          F.removeFnAttr(Attribute::AttrKind::MinSize);
        if (F.hasFnAttribute(Attribute::AttrKind::OptimizeForSize))
          F.removeFnAttr(Attribute::AttrKind::OptimizeForSize);
        if (!F.hasOptNone())
          F.addFnAttr(Attribute::AttrKind::OptimizeNone);
        HandleInlineHook(&F);
        if (AntiRebindSymbol)
          for (Instruction &I : instructions(F))
            if (isa<CallInst>(&I) || isa<InvokeInst>(&I)) {
              CallSite CS(&I);
              Function *Called = CS.getCalledFunction();
              if (!Called)
                Called = dyn_cast<Function>(
                    CS.getCalledValue()->stripPointerCasts());
              if (Called && Called->isDeclaration() &&
                  Called->isExternalLinkage(Called->getLinkage()) &&
                  !Called->isIntrinsic() &&
                  !Called->getName().startswith("clang.")) {
                GlobalVariable *GV = cast<GlobalVariable>(M.getOrInsertGlobal(
                    ("AntiRebindSymbol_" + Called->getName()).str(),
                    Called->getType()));
                if (!GV->hasInitializer()) {
                  GV->setConstant(true); // make the gv not writable
                  GV->setInitializer(Called);
                  GV->setLinkage(GlobalValue::LinkageTypes::PrivateLinkage);
                }
                appendToCompilerUsed(M, {GV});
                Value *Load =
                    new LoadInst(GV->getValueType(), GV, Called->getName(), &I);
                Value *BitCasted = BitCastInst::CreateBitOrPointerCast(
                    Load, CS.getCalledValue()->getType(), "", &I);
                CS.setCalledFunction(BitCasted);
              }
            }
      }
    }
    if (hasobjcmethod) {
      for (GlobalVariable &GV : M.globals()) {
        if (GV.hasName() && GV.hasInitializer() &&
            GV.getSection() != "llvm.ptrauth" &&
            (GV.getName().startswith("_OBJC_$_INSTANCE_METHODS") ||
             GV.getName().startswith("_OBJC_$_CLASS_METHODS"))) {
          GlobalVariable *methodListGV = &GV;
          ConstantStruct *methodListStruct =
              cast<ConstantStruct>(methodListGV->getInitializer());
          ConstantArray *method_list =
              cast<ConstantArray>(methodListStruct->getOperand(2));
          for (unsigned i = 0; i < method_list->getNumOperands(); i++) {
            ConstantStruct *methodStruct =
                cast<ConstantStruct>(method_list->getOperand(i));
            GlobalVariable *SELNameGV = cast<GlobalVariable>(
                opaquepointers ? methodStruct->getOperand(0)
                               : methodStruct->getOperand(0)->getOperand(0));
            ConstantDataSequential *SELNameCDS =
                cast<ConstantDataSequential>(SELNameGV->getInitializer());
            bool classmethod = GV.getName().startswith("_OBJC_$_CLASS_METHODS");
            string classname =
                GV.getName()
                    .substr(strlen(classmethod ? "_OBJC_$_CLASS_METHODS_"
                                               : "_OBJC_$_INSTANCE_METHODS_"))
                    .str();
            string selname = SELNameCDS->getAsCString().str();
            Function *IMPFunc = cast<Function>(
                appleptrauth
                    ? opaquepointers
                          ? cast<GlobalVariable>(methodStruct->getOperand(2))
                                ->getInitializer()
                                ->getOperand(0)
                          : cast<ConstantExpr>(
                                cast<GlobalVariable>(
                                    methodStruct->getOperand(2)->getOperand(0))
                                    ->getInitializer()
                                    ->getOperand(0))
                                ->getOperand(0)
                : opaquepointers ? methodStruct->getOperand(2)
                                 : methodStruct->getOperand(2)->getOperand(0));
            if (!toObfuscate(flag, IMPFunc, "antihook"))
              continue;
            HandleObjcRuntimeHook(IMPFunc, classname, selname, classmethod);
          }
        }
      }
    }
    genMachineCodes(M);
    return true;
  } // End runOnFunction

  void genMachineCodes(Module &M) {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    InitializeAllAsmParsers();
    InitializeAllDisassemblers();
    InitializeAllTargetMCAs();

    RTDyldMemoryManager *MemMgr = new SectionMemoryManager();

    ExecutionEngine *EE =
        EngineBuilder(CloneModule(M))
            .setEngineKind(EngineKind::Kind::JIT)
            .setOptLevel(CodeGenOpt::Level::None)
            .setVerifyModules(true)
            .setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager>(MemMgr))
            .create();
    EE->finalizeObject();
    for (Function &F : M)
      if (GlobalVariable *GV = funcname2gv[F.getName().str()])
        GV->setInitializer(ConstantInt::get(
            Type::getInt32Ty(M.getContext()),
            *(uint32_t *)EE->getFunctionAddress(F.getName().str())));
  }

  void HandleInlineHook(Function *F) {
    /*
   We split the originalBB A into:
      A < - InlineHook Detection
      | \
      |  B for handler()
      | /
      C < - Original Following BB
   */

    Type *Int32Ty = Type::getInt32Ty(F->getParent()->getContext());

    GlobalVariable *GV = new GlobalVariable(
        *F->getParent(), Int32Ty, true,
        GlobalValue::LinkageTypes::PrivateLinkage,
        ConstantInt::getNullValue(Int32Ty), F->getName() + "uint32Signature");
    funcname2gv[F->getName().str()] = GV;
    BasicBlock *A = &(F->getEntryBlock());
    BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
    BasicBlock *B =
        BasicBlock::Create(A->getContext(), "HookDetectedHandler", F, C);
    // Change A's terminator to jump to B
    // We'll add new terminator in B to jump C later
    A->getTerminator()->eraseFromParent();

    IRBuilder<> IRBA(A);
    IRBuilder<> IRBB(B);

    Value *toDetect = IRBA.CreateLoad(Int32Ty, F);
    Value *ICmpNE =
        IRBA.CreateICmpNE(toDetect, IRBA.CreateLoad(GV->getValueType(), GV));
    IRBA.CreateCondBr(ICmpNE, B, C);
    CreateCallbackAndJumpBack(&IRBB, C);
  }

  void HandleObjcRuntimeHook(Function *ObjcMethodImp, string classname,
                             string selname, bool classmethod) {
    /*
    We split the originalBB A into:
       A < - RuntimeHook Detection
       | \
       |  B for handler()
       | /
       C < - Original Following BB
    */
    Module *M = ObjcMethodImp->getParent();

    BasicBlock *A = &(ObjcMethodImp->getEntryBlock());
    BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
    BasicBlock *B = BasicBlock::Create(A->getContext(), "HookDetectedHandler",
                                       ObjcMethodImp, C);
    // Delete A's terminator
    A->getTerminator()->eraseFromParent();

    IRBuilder<> IRBA(A);
    IRBuilder<> IRBB(B);

    Type *Int8PtrTy = Type::getInt8PtrTy(M->getContext());

    Value *GetClass = IRBA.CreateCall(M->getFunction("objc_getClass"),
                                      {IRBA.CreateGlobalStringPtr(classname)});
    Value *GetSelector = IRBA.CreateCall(M->getFunction("sel_registerName"),
                                         {IRBA.CreateGlobalStringPtr(selname)});
    Value *GetMethod =
        IRBA.CreateCall(M->getFunction(classmethod ? "class_getClassMethod"
                                                   : "class_getInstanceMethod"),
                        {GetClass, GetSelector});
    Value *GetMethodImp = IRBA.CreateCall(
        M->getFunction("method_getImplementation"), {GetMethod});
    Value *IcmpEq =
        IRBA.CreateICmpEQ(IRBA.CreateBitCast(GetMethodImp, Int8PtrTy),
                          ConstantExpr::getBitCast(ObjcMethodImp, Int8PtrTy));
    IRBA.CreateCondBr(IcmpEq, C, B);
    CreateCallbackAndJumpBack(&IRBB, C);
  }
  void CreateCallbackAndJumpBack(IRBuilder<> *IRBB, BasicBlock *C) {
    Module *M = C->getModule();
    Function *AHCallBack = M->getFunction("AHCallBack");
    if (AHCallBack) {
      IRBB->CreateCall(AHCallBack);
    } else {
      if (triple.isOSDarwin() && triple.isAArch64()) {
        string exitsvcasm = "mov w16, #1\n";
        exitsvcasm += "svc #" + to_string(cryptoutils->get_range(65536)) + "\n";
        InlineAsm *IA =
            InlineAsm::get(FunctionType::get(IRBB->getVoidTy(), false),
                           exitsvcasm, "", true, false);
        IRBB->CreateCall(IA);
      } else {
        FunctionType *ABFT =
            FunctionType::get(Type::getVoidTy(M->getContext()), false);
        Function *abort_declare =
            cast<Function>(M->getOrInsertFunction("abort", ABFT).getCallee());
        abort_declare->addFnAttr(Attribute::AttrKind::NoReturn);
        IRBB->CreateCall(abort_declare);
      }
    }
    IRBB->CreateBr(C);
  }
};
} // namespace llvm

ModulePass *llvm::createAntiHookPass(bool flag) { return new AntiHook(flag); }
char AntiHook::ID = 0;
INITIALIZE_PASS(AntiHook, "antihook", "AntiHook", true, true)
