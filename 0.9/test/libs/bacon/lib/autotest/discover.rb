Autotest.add_discovery do
  if File.exist?('spec/.bacon') || File.exist?('test/.bacon')
    class Autotest
      @@discoveries.delete_if { |d| d.inspect =~ /rspec/ }
      warn 'Removing rspec from autotest!'
    end
    'bacon'
  end
end