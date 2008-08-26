#ifndef __OBJC_H_
#define __OBJC_H_

static inline void
rb_objc_install_method(Class klass, SEL sel, IMP imp)
{
    Method method, method2;

    method = class_getInstanceMethod(klass, sel);
    assert(method != NULL);
 
    method2 = class_getInstanceMethod((Class)RCLASS_SUPER(klass), sel);
    if (method == method2)  {
	assert(class_addMethod(klass, sel, imp, method_getTypeEncoding(method)));
    }
    else {
	method_setImplementation(method, imp);
    }
}

static inline void
rb_objc_install_method2(Class klass, const char *selname, IMP imp)
{
    rb_objc_install_method(klass, sel_registerName(selname), imp);
}

#endif /* __OBJC_H_ */
