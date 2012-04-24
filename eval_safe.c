/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

/*
 * This file is included by eval.c
 */

/* safe-level:
   0 - strings from streams/environment/ARGV are tainted (default)
   1 - no dangerous operation by tainted value
   2 - process/file operations prohibited
   3 - all generated objects are tainted
   4 - no global (non-tainted) variable modification/no direct output
*/

#define SAFE_LEVEL_MAX 4

/* $SAFE accessor */

int
rb_safe_level(void)
{
    return rb_vm_safe_level();
}

void
rb_set_safe_level_force(int safe)
{
    rb_vm_set_safe_level(safe);
}

void
rb_set_safe_level(int level)
{
    if (level > rb_vm_safe_level()) {
	if (level > SAFE_LEVEL_MAX) {
	    level = SAFE_LEVEL_MAX;
	}
	rb_vm_set_safe_level(level);
    }
}

static VALUE
safe_getter(void)
{
    return INT2NUM(rb_safe_level());
}

static void
safe_setter(VALUE val)
{
    int level = NUM2INT(val);
    int current_level = rb_vm_safe_level();

    if (level < current_level) {
	rb_raise(rb_eSecurityError,
		 "tried to downgrade safe level from %d to %d",
		 current_level, level);
    }
    if (level > SAFE_LEVEL_MAX) {
	level = SAFE_LEVEL_MAX;
    }
    rb_vm_set_safe_level(level);
}

void
rb_secure(int level)
{
    if (level <= rb_safe_level()) {
	if (rb_frame_callee()) {
	    rb_raise(rb_eSecurityError, "Insecure operation `%s' at level %d",
		     rb_id2name(rb_frame_callee()), rb_safe_level());
	}
	else {
	    rb_raise(rb_eSecurityError, "Insecure operation at level %d",
		     rb_safe_level());
	}
    }
}

void
rb_secure_update(VALUE obj)
{
    if (!OBJ_TAINTED(obj))
	rb_secure(4);
}

void
rb_insecure_operation(void)
{
    if (rb_frame_callee()) {
	rb_raise(rb_eSecurityError, "Insecure operation - %s",
		 rb_id2name(rb_frame_callee()));
    }
    else {
	rb_raise(rb_eSecurityError, "Insecure operation: -r");
    }
}

void
rb_check_safe_obj(VALUE x)
{
    if (rb_safe_level() > 0 && OBJ_TAINTED(x)) {
	rb_insecure_operation();
    }
    rb_secure(4);
}

void
rb_check_safe_str(VALUE x)
{
    rb_check_safe_obj(x);
    if (TYPE(x) != T_STRING) {
	rb_raise(rb_eTypeError, "wrong argument type %s (expected String)",
		 rb_obj_classname(x));
    }
}
