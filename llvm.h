#ifndef __MR_LLVM_H_
#define __MR_LLVM_H_

#if MACRUBY_STATIC
# include <string>
# include <vector>
# include <map>
#else

// This file must be included at the very beginning of every C++ file in the
// project, due to type collisions between LLVM and the MRI C API.

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/CallingConv.h>
#include <llvm/Instructions.h>
#include <llvm/Intrinsics.h>
#include <llvm/Analysis/DebugInfo.h>
#if !defined(LLVM_TOT)
# include <llvm/Analysis/DIBuilder.h>
#endif
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
using namespace llvm;

#endif // !MACRUBY_STATIC

#endif // __MR_LLVM_H_
