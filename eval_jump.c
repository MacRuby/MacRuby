/* -*-c-*- */
/*
 * from eval.c
 */

/* exit */

// TODO: move & lock me into RoxorCore
static VALUE at_exit_procs = Qnil;

/*
 *  call-seq:
 *     at_exit { block } -> proc
 *
 *  Converts _block_ to a +Proc+ object (and therefore
 *  binds it at the point of call) and registers it for execution when
 *  the program exits. If multiple handlers are registered, they are
 *  executed in reverse order of registration.
 *
 *     def do_at_exit(str1)
 *       at_exit { print str1 }
 *     end
 *     at_exit { puts "cruel world" }
 *     do_at_exit("goodbye ")
 *     exit
 *
 *  <em>produces:</em>
 *
 *     goodbye cruel world
 */

static VALUE
rb_f_at_exit(VALUE self, SEL sel)
{
    if (!rb_block_given_p()) {
	rb_raise(rb_eArgError, "called without a block");
    }
    VALUE proc = rb_block_proc();
    rb_ary_push(at_exit_procs, proc);
    return proc;
}

void
rb_exec_end_proc(void)
{
    while (true) {
	const int count = RARRAY_LEN(at_exit_procs);
	if (count > 0) {
	    VALUE proc = RARRAY_AT(at_exit_procs, count - 1);
	    rb_ary_delete_at(at_exit_procs, count - 1);	
	    rb_proc_call2(proc, 0, NULL);
	    continue;
	}
	break;
    }
}

void
Init_jump(void)
{
    rb_objc_define_module_function(rb_mKernel, "at_exit", rb_f_at_exit, 0);

    at_exit_procs = rb_ary_new();
    GC_RETAIN(at_exit_procs);
}
