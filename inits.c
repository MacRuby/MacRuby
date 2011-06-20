/*
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"

void Init_Array(void);
void Init_Bignum(void);
void Init_Binding(void);
void Init_Comparable(void);
void Init_Complex(void);
void Init_Dir(void);
void Init_Enumerable(void);
void Init_Enumerator(void);
void Init_Exception(void);
void Init_syserr(void);
void Init_eval(void);
void Init_load(void);
void Init_Proc(void);
void Init_File(void);
void Init_GC(void);
void Init_Hash(void);
void Init_ENV(void);
void Init_IO(void);
void Init_Math(void);
void Init_marshal(void);
void Init_Numeric(void);
void Init_Object(void);
void Init_Class(void);
void Init_pack(void);
void Init_Precision(void);
void Init_Symbol(void);
void Init_PreSymbol(void);
void Init_id(void);
void Init_process(void);
void Init_Random(void);
void Init_Range(void);
void Init_Rational(void);
void Init_Regexp(void);
void Init_signal(void);
void Init_String(void);
void Init_Struct(void);
void Init_Time(void);   
void Init_var_tables(void);
void Init_version(void);
void Init_VM(void);
void Init_Thread(void);
//void Init_Cont(void);
void Init_Encoding(void);
void Init_PostGC(void);
void Init_ObjC(void);
void Init_BridgeSupport(void);
void Init_FFI(void);
void Init_Dispatch(void);
void Init_Transcode(void);
void Init_PostVM(void);
void Init_sandbox(void);

void
rb_call_inits()
{
    Init_PreSymbol();
    Init_id();
    Init_var_tables();
    Init_Object();
    Init_Class();
    Init_VM();
    Init_Encoding();
    Init_Comparable();
    Init_Enumerable();
    Init_Precision();
    Init_String();
    Init_Symbol();
    Init_Exception();
    Init_eval();
    Init_jump();
    Init_Numeric();
    Init_Bignum();
    Init_syserr();
    Init_Array();
    Init_Hash();
    Init_ENV();
    Init_Struct();
    Init_Regexp();
    Init_pack();
    Init_marshal();
    Init_Range();
    Init_IO();
    Init_Dir();
    Init_Time();
    Init_Random();
    Init_signal();
    Init_process();
    Init_load();
    Init_Proc();
    Init_Binding();
    Init_Math();
    Init_GC();
    Init_Enumerator();
    Init_Thread();
    //Init_Cont();
    Init_Rational();
    Init_Complex();
    Init_version();
    Init_PostGC();
    Init_ObjC();
    Init_BridgeSupport();
    Init_FFI();
    Init_Dispatch();
    Init_Transcode();
    Init_sandbox();
    Init_PostVM();
}
