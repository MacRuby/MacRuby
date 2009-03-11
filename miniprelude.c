#include "ruby/ruby.h"
#include "ruby/node.h"
#include "roxor.h"

static const char prelude_name0[] = "prelude.rb";
static const char prelude_code0[] =
"\n"
"# Mutex\n"
"\n"
"class Mutex\n"
"  def synchronize\n"
"    self.lock\n"
"    begin\n"
"      yield\n"
"    ensure\n"
"      self.unlock rescue nil\n"
"    end\n"
"  end\n"
"end\n"
"\n"
"# Thread\n"
"\n"
"class Thread\n"
"  MUTEX_FOR_THREAD_EXCLUSIVE = Mutex.new\n"
"  def self.exclusive\n"
"    MUTEX_FOR_THREAD_EXCLUSIVE.synchronize{\n"
"      yield\n"
"    }\n"
"  end\n"
"end\n"
"\n"
"def require_relative(relative_feature)\n"
"  c = caller.first\n"
"  e = c.rindex(/:\\d+:in /)\n"
"  file = $`\n"
"  if /\\A\\((.*)\\)/ =~ file # eval, etc.\n"
"    raise LoadError, \"require_relative is called in #{$1}\"\n"
"  end\n"
"  absolute_feature = File.expand_path(File.join(File.dirname(file), relative_feature))\n"
"  require absolute_feature\n"
"end\n"
;

void
Init_prelude(void)
{
  rb_vm_run_node(prelude_name0, rb_compile_string(
    prelude_name0,
    rb_str_new(prelude_code0, sizeof(prelude_code0) - 1),
    1));

}
