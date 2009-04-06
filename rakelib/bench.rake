namespace :bench do

  desc "Run the regression performance suite"
  task :ci do
    sh "./miniruby -I./lib bench.rb"
  end

  # We cannot run all the benchmarks yet, so we only run a selection for now.
  YARV_BENCHMARKS = %w{
    bm_app_answer.rb bm_app_factorial.rb bm_app_fib.rb bm_app_raise.rb
    bm_app_tak.rb bm_app_tarai.rb bm_app_mergesort.rb bm_loop_times.rb
    bm_loop_whileloop.rb bm_so_ackermann.rb bm_so_object.rb bm_so_random.rb
    bm_so_nested_loop.rb bm_vm1_block.rb bm_vm1_const.rb bm_vm1_ivar.rb
    bm_vm1_ivar_set.rb bm_vm1_ensure.rb bm_vm1_rescue.rb bm_vm1_simplereturn.rb
    bm_vm2_eval.rb bm_vm2_poly_method.rb bm_vm2_proc.rb bm_vm2_send.rb
    bm_vm2_method.rb bm_vm2_super.rb bm_vm2_unif1.rb bm_vm2_zsuper.rb
  } 

  desc "Run the YARV benchmarks"
  task :yarv do
    YARV_BENCHMARKS.each do |file|
      path = File.join('benchmark', file)
      sh "/usr/bin/ruby b.rb #{path}"
    end 
  end

end
