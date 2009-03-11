/* -*-c-*- */
/*
 * from eval.c
 */

/* exit */

void
rb_call_end_proc(VALUE data)
{
    rb_proc_call(data, rb_ary_new());
}

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
    VALUE proc;

    if (!rb_block_given_p()) {
	rb_raise(rb_eArgError, "called without a block");
    }
    proc = rb_block_proc();
    rb_set_end_proc(rb_call_end_proc, proc);
    return proc;
}

struct end_proc_data {
    void (*func) ();
    VALUE data;
    int safe;
    struct end_proc_data *next;
};

static struct end_proc_data *end_procs = NULL;
static struct end_proc_data *tmp_end_procs = NULL;

void
rb_set_end_proc(void (*func)(VALUE), VALUE data)
{
    struct end_proc_data *link = ALLOC(struct end_proc_data);
    struct end_proc_data **list;

    list = &end_procs;
    GC_WB(&link->next, *list);
    link->func = func;
    link->data = data;
    link->safe = rb_safe_level();
    *list = link;
}

void
rb_exec_end_proc(void)
{
    struct end_proc_data *link, *tmp;
    int safe = rb_safe_level();

    if (end_procs != NULL) {
	tmp_end_procs = link = end_procs;
	end_procs = 0;
	while (link != NULL) {
	    rb_set_safe_level_force(link->safe);
	    (*link->func) (link->data);
	    tmp = link;
	    tmp_end_procs = link = link->next;
	    xfree(tmp);
	}
    }
    rb_set_safe_level_force(safe);
}

void
Init_jump(void)
{
    rb_objc_define_method(rb_mKernel, "at_exit", rb_f_at_exit, 0);
    GC_ROOT(&end_procs);
}
