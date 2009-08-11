#ifndef __MR_LLVM_H_
#define __MR_LLVM_H_

// This file must be included at the very beginning of every C++ file in the
// project, due to type collisions between LLVM and the MRI C API.

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/CallingConv.h>
#include <llvm/Instructions.h>
#include <llvm/ModuleProvider.h>
#include <llvm/Intrinsics.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
using namespace llvm;

#endif // __MR_LLVM_H_
