/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 2007 Koichi Sasada
 */

#ifndef RUBY_ID_H
#define RUBY_ID_H

extern VALUE symIFUNC;
extern VALUE symCFUNC;

extern ID idPLUS;
extern ID idMINUS;
extern ID idMULT;
extern ID idDIV;
extern ID idMOD;
extern ID idLT;
extern ID idLTLT;
extern ID idLE;
extern ID idGT;
extern ID idGE;
extern ID idEq;
extern ID idEqq;
extern ID idNeq;
extern ID idNot;
extern ID idBackquote;
extern ID idEqTilde;
extern ID idThrowState;
extern ID idAREF;
extern ID idASET;
extern ID idIntern;
extern ID idMethodMissing;
extern ID idLength;
extern ID idGets;
extern ID idSucc;
extern ID idEach;
extern ID idLambda;
extern ID idRangeEachLT;
extern ID idRangeEachLE;
extern ID idArrayEach;
extern ID idTimes;
extern ID idEnd;
extern ID idSend;
extern ID id__send__;
extern ID idRespond_to;
extern ID idInitialize;
extern ID idAttached;
#if WITH_OBJC
extern SEL selPLUS;
extern SEL selMINUS;
extern SEL selMULT;
extern SEL selDIV;
extern SEL selMOD;
extern SEL selAND;
extern SEL selEq;
extern SEL selNeq;
extern SEL selCmp;
extern SEL selLT;
extern SEL selLE;
extern SEL selGT;
extern SEL selGE;
extern SEL selLTLT;
extern SEL selAREF;
extern SEL selASET;
extern SEL selLength;
extern SEL selSucc;
extern SEL selNot;
extern SEL selNot2;
extern SEL selAlloc;
extern SEL selAllocWithZone;
extern SEL selCopyWithZone;
extern SEL selInit;
extern SEL selInitialize;
extern SEL selInitialize2;
extern SEL selInitializeCopy;
extern SEL selInitializeClone;
extern SEL selInitializeDup;
extern SEL selDescription;
extern SEL selInspect;
extern SEL selNew;
extern SEL selRespondTo;
extern SEL selMethodMissing;
extern SEL selConstMissing;
extern SEL selCopy;
extern SEL selMutableCopy;
extern SEL selToS;
extern SEL selToAry;
extern SEL sel_zone;
extern SEL selSend;
extern SEL sel__send__;
extern SEL selCall;
extern SEL selEqTilde;
extern SEL selClass;
extern SEL selEval;
extern SEL selInstanceEval;
extern SEL selClassEval;
extern SEL selModuleEval;
extern SEL selLocalVariables;
extern SEL selBinding;
extern SEL selNesting;
extern SEL selConstants;
extern SEL selEach;
extern SEL selEqq;
extern SEL selDup;
extern SEL selBackquote;
extern SEL selMethodAdded;
extern SEL selSingletonMethodAdded;
extern SEL selMethodRemoved;
extern SEL selSingletonMethodRemoved;
extern SEL selMethodUndefined;
extern SEL selSingletonMethodUndefined;
extern SEL selIsEqual;
extern SEL selWrite;
extern SEL selInherited;
extern SEL selLambda;
extern SEL selObjectForKey;
extern SEL selSetObjectForKey;
extern SEL selFinalize;
extern SEL sel__method__;
extern SEL sel__callee__;
extern ID idIncludedModules;
extern ID idIncludedInClasses;
extern ID idAncestors;
#endif
#endif /* RUBY_ID_H */
