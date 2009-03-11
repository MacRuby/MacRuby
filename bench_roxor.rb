core_features = [
  'bm_app_answer.rb',
  'bm_app_factorial.rb',
  'bm_app_fib.rb',
  'bm_app_raise.rb',
  'bm_app_tak.rb' ,
  'bm_app_tarai.rb',
  'bm_app_mergesort.rb',
  'bm_loop_times.rb',
  'bm_loop_whileloop.rb',
  'bm_so_ackermann.rb',
#  'bm_so_nested_loop.rb',
  'bm_so_object.rb',
  'bm_so_random.rb',
  'bm_vm1_block.rb',
  'bm_vm1_const.rb',
  'bm_vm1_ivar.rb',
  'bm_vm1_ivar_set.rb',
  'bm_vm1_ensure.rb',
#  'bm_vm1_length.rb',
  'bm_vm1_rescue.rb',
  'bm_vm1_simplereturn.rb',
#  'bm_vm1_swap.rb',
  'bm_vm2_eval.rb',
  'bm_vm2_poly_method.rb',
  'bm_vm2_proc.rb',
  'bm_vm2_send.rb',
  'bm_vm2_method.rb',
  'bm_vm2_super.rb',
  'bm_vm2_unif1.rb',
  'bm_vm2_zsuper.rb'
]

def bench(rubies, file, quiet)
  rubies.map do |ruby|
    $stderr.puts "    " + `#{ruby} -v`.strip unless quiet
    line = "#{ruby} #{file}"
    best = 1e10
    3.times do 
      t = Time.now
      v = system(line) ? Time.now - t : nil
      $stderr.puts "        " + v.to_s unless quiet
      if v and (best == nil or best > v)
        best = v  
      end
    end
    [ruby, best]
  end
end

our_ruby = File.join(Dir.pwd, 'miniruby')
rubies = [our_ruby, '/usr/local/bin/ruby19']
#rubies = [our_ruby, '/usr/local/bin/ruby19', '/usr/bin/ruby']

quiet = ARGV[0] == '--quiet'

Dir.chdir('benchmark') do 
  core_features.each do |file|
    $stderr.puts file + ' ...'
    results = bench(rubies, file, quiet)
    ok = results.sort { |x, y| x[1] <=> y[1] }[0][0].include?(our_ruby)
    $stderr.puts '    ' + (ok ? 'PASS' : 'FAIL')
  end
end
