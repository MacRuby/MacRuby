/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 2004-2007 Koichi Sasada
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"

#define extern
#include "id.h"
#undef extern

void
Init_id(void)
{
    /* Symbols */
    symIFUNC = ID2SYM(rb_intern("<IFUNC>"));
    symCFUNC = ID2SYM(rb_intern("<CFUNC>"));

    /* IDs */
    idPLUS = rb_intern("+");
    idMINUS = rb_intern("-");
    idMULT = rb_intern("*");
    idDIV = rb_intern("/");
    idMOD = rb_intern("%");
    idLT = rb_intern("<");
    idLTLT = rb_intern("<<");
    idLE = rb_intern("<=");
    idGT = rb_intern(">");
    idGE = rb_intern(">=");
    idEq = rb_intern("==");
    idEqq = rb_intern("===");
    idBackquote = rb_intern("`");
    idEqTilde = rb_intern("=~");
    idNot = rb_intern("!");
    idNeq = rb_intern("!=");
    idAttached = rb_intern("__attached__");
#if WITH_OBJC
    selPLUS = sel_registerName("+:");
    selMINUS = sel_registerName("-:");
    selMULT = sel_registerName("*:");
    selDIV = sel_registerName("/:");
    selMOD = sel_registerName("%:");
    selAND = sel_registerName("&:");
    selEq = sel_registerName("==:");
    selNeq = sel_registerName("!=:");
    selCmp = sel_registerName("<=>:");
    selLT = sel_registerName("<:");
    selLE = sel_registerName("<=:");
    selGT = sel_registerName(">:");
    selGE = sel_registerName(">=:");
    selLTLT = sel_registerName("<<:");
    selAREF = sel_registerName("[]:");
    selASET = sel_registerName("[]=:");
    selLength = sel_registerName("length");
    selSucc = sel_registerName("succ");
    selNot = sel_registerName("!");
    selNot2 = sel_registerName("!:");
    selAlloc = sel_registerName("alloc");
    selAllocWithZone = sel_registerName("allocWithZone:");
    selCopyWithZone = sel_registerName("copyWithZone:");
    selInit = sel_registerName("init");
    selInitialize = sel_registerName("initialize");
    selInitialize2 = sel_registerName("initialize:");
    selInitializeCopy = sel_registerName("initialize_copy:");
    selInitializeClone = sel_registerName("initialize_clone:");
    selInitializeDup = sel_registerName("initialize_dup:");
    selDescription = sel_registerName("description");
    selInspect = sel_registerName("inspect");
    selNew = sel_registerName("new");
    selRespondTo = sel_registerName("respond_to?:");
    selMethodMissing = sel_registerName("method_missing:");
    selConstMissing = sel_registerName("const_missing:");
    selCopy = sel_registerName("copy");
    selMutableCopy = sel_registerName("mutableCopy");
    sel_zone = sel_registerName("zone");
    selToS = sel_registerName("to_s");
    selToAry = sel_registerName("to_ary");
    selSend = sel_registerName("send:");
    sel__send__ = sel_registerName("__send__:");
    selCall = sel_registerName("call:");
    selEqTilde = sel_registerName("=~:");
    selClass = sel_registerName("class");
    selEval = sel_registerName("eval:");
    selInstanceEval = sel_registerName("instance_eval:");
    selClassEval = sel_registerName("class_eval:");
    selModuleEval = sel_registerName("module_eval:");
    selLocalVariables = sel_registerName("local_variables");
    selBinding = sel_registerName("binding");
    selNesting = sel_registerName("nesting");
    selConstants = sel_registerName("constants");
    selEach = sel_registerName("each");
    selEqq = sel_registerName("===:");
    selDup = sel_registerName("dup");
    selBackquote = sel_registerName("`:");
    selMethodAdded = sel_registerName("method_added:");
    selSingletonMethodAdded = sel_registerName("singleton_method_added:");
    selMethodRemoved = sel_registerName("method_removed:");
    selSingletonMethodRemoved = sel_registerName("singleton_method_removed:");
    selMethodUndefined = sel_registerName("method_undefined:");
    selSingletonMethodUndefined = sel_registerName("singleton_method_undefined:");
    selIsEqual = sel_registerName("isEqual:");
    selWrite = sel_registerName("write:");
    selInherited = sel_registerName("inherited:");
    selLambda = sel_registerName("lambda");
    selObjectForKey = sel_registerName("objectForKey:");
    selSetObjectForKey = sel_registerName("setObject:forKey:");
    selFinalize = sel_registerName("finalize");

    sel__method__= sel_registerName("__method__");
    sel__callee__ = sel_registerName("__callee__");
#endif

    idAREF = rb_intern("[]");
    idASET = rb_intern("[]=");

    idEach = rb_intern("each");
    idTimes = rb_intern("times");
    idLength = rb_intern("length");
    idLambda = rb_intern("lambda");
    idIntern = rb_intern("intern");
    idGets = rb_intern("gets");
    idSucc = rb_intern("succ");
    idEnd = rb_intern("end");
    idRangeEachLT = rb_intern("Range#each#LT");
    idRangeEachLE = rb_intern("Range#each#LE");
    idArrayEach = rb_intern("Array#each");
    idMethodMissing = rb_intern("method_missing");

    idThrowState = rb_intern("#__ThrowState__");

    idSend = rb_intern("send");
    id__send__ = rb_intern("__send__");

    idRespond_to = rb_intern("respond_to?");
    idInitialize = rb_intern("initialize");

    idIncludedModules = rb_intern("__included_modules__");
    idIncludedInClasses = rb_intern("__included_in_classes__");
    idAncestors = rb_intern("__ancestors__");
}
