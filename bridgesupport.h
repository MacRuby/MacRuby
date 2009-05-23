#ifndef __BOXED_H_
#define __BOXED_H_

#if defined(__cplusplus)

#include "bs.h"

typedef struct rb_vm_bs_boxed {
    bs_element_type_t bs_type;
    bool is_struct(void) { return bs_type == BS_ELEMENT_STRUCT; }
    union {
	bs_element_struct_t *s;
	bs_element_opaque_t *o;
	void *v;
    } as;
    Type *type;
    VALUE klass;
} rb_vm_bs_boxed_t;

#endif /* __cplusplus */

#endif /* __BOXED_H_ */
