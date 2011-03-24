Autotest.add_hook :initialize do |att|
  att.clear_mappings

  att.add_mapping(%r%^(test|spec)/.*\.rb$%) do |filename, _|
    filename
  end

  att.add_mapping(%r%^lib/(.*)\.rb$%) do |filename, m|
    lib_path = m[1]
    spec = File.basename(lib_path)
    path = File.dirname(lib_path)
    [
      "test/#{path}/test_#{spec}.rb",
      "test/#{path}/spec_#{spec}.rb",
      "spec/#{path}/spec_#{spec}.rb",
      # TODO : decide if the follow 'rspec style' name should be allowed?
      # "spec/#{path}/#{spec}_spec.rb"
    ]
  end

  false
end

class Autotest::Bacon < Autotest
  def initialize
    super
    self.libs = %w[. lib test spec].join(File::PATH_SEPARATOR)
  end

  def make_test_cmd(files_to_test)
    args = files_to_test.keys.flatten.join(' ')
    args = '-a' if args.empty?
    # TODO : make regex to pass to -n using values
    "#{ruby} -S bacon -I#{libs} -o TestUnit #{args}"
  end
end
