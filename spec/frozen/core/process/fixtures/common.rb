module ProcessSpecs
  class Daemonizer
    attr_reader :input, :data, :signal

    def initialize
      @script = fixture __FILE__, "daemon.rb"
      @input = tmp("process_daemon_input_file")
      @data = tmp("process_daemon_data_file")
      @signal = tmp("process_daemon_signal_file")
      @args = []
    end

    def wait_for_daemon
      while true
        return if File.exists? @signal
        sleep 0.1
      end
    end

    def invoke(behavior, arguments=[])
      args = Marshal.dump(arguments).unpack("H*")
      args << @input
      args << @data
      args << @signal
      args << behavior

      ruby_exe @script, :args => args

      wait_for_daemon

      if 'daemon_at_exit' == behavior || /keep_stdio_open_true_stdout/ =~ behavior
        # prevent false negative because of filesystem cache or something
        10.times do
          break if File.exist?(@data) && File.size?(@data)
          sleep 0.1
        end
      end

      return unless File.exists? @data

      File.open(@data, "rb") { |f| return f.gets.tap{|x|x&&x.chomp!} }
    end
  end
end
