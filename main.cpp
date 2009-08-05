/*
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#undef RUBY_EXPORT
#include "ruby.h"
#include "ruby/node.h"
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

extern "C" {
    void rb_vm_print_current_exception(void);
    void rb_vm_aot_compile(NODE *);
    void rb_vm_init_compiler(void);
}

int
main(int argc, char **argv, char **envp)
{
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
#endif

    try {
	ruby_sysinit(&argc, &argv);
	ruby_init();
	void *node = ruby_options(argc, argv);
	rb_vm_init_compiler();
	if (ruby_aot_compile) {
	    rb_vm_aot_compile((NODE *)node);
	    rb_exit(0);
	}
	else {	
	    rb_exit(ruby_run_node(node));
	}
    }
    catch (...) {
	rb_vm_print_current_exception();
	rb_exit(1);
    }
}
