class StringIO

  attr_reader :string, :pos
  
  #   strio.lineno    -> integer
  #
  # Returns the current line number in *strio*. The stringio must be
  # opened for reading. +lineno+ counts the number of times  +gets+ is
  # called, rather than the number of newlines  encountered. The two
  # values will differ if +gets+ is  called with a separator other than
  # newline.  See also the  <code>$.</code> variable. 
  #
  #
  #   strio.lineno = integer    -> integer
  #
  # Manually sets the current line number to the given value.
  # <code>$.</code> is updated only on the next read. 
  #
  attr_accessor :lineno
  
  include Enumerable  
  
  #    StringIO.open(string=""[, mode]) {|strio| ...}
  #
  # Equivalent to StringIO.new except that when it is called with a block, it
  # yields with the new instance and closes it, and returns the result which
  # returned from the block.
  #
  def self.open(*args)
    obj = new(*args)
    
    if block_given?
      begin
        yield obj
      ensure
        obj.close
        obj.instance_variable_set(:@string, nil)
        obj
      end
    else
      obj
    end
  end

  # StringIO.new(string=""[, mode])
  #
  # Creates new StringIO instance from with _string_ and _mode_.
  #
  def initialize(string = ByteString.new, mode = nil)
    @string = string.to_str  
    @pos = 0
    @lineno = 0
    define_mode(mode)
    
    raise Errno::EACCES if (@writable && string.frozen?)   
    self
  end
  
  def initialize_copy(from)
    from = from.to_strio
    self.taint if from.tainted?
 
    @string   = from.instance_variable_get(:@string).dup
    # mode
    @append   = from.instance_variable_get(:@append)
    @readable = from.instance_variable_get(:@readable)
    @writable = from.instance_variable_get(:@writable)
 
    @pos = from.instance_variable_get(:@pos)
    @lineno = from.instance_variable_get(:@lineno)
 
    self
  end
  
  #   strio.reopen(other_StrIO)     -> strio
  #   strio.reopen(string, mode)    -> strio
  #
  # Reinitializes *strio* with the given <i>other_StrIO</i> or _string_ 
  # and _mode_ (see StringIO#new).
  #
  def reopen(str=nil, mode=nil)
    if str == nil && mode == nil
      mode = 'w+'
    elsif !str.kind_of?(String) && mode == nil
      self.taint if str.tainted?
      raise TypeError unless str.respond_to?(:to_strio)
      @string = str.to_strio.string 
    else
      raise TypeError unless str.respond_to?(:to_str)
      @string = str.to_str  
    end
    
    define_mode(mode)
    @pos = 0
    @lineno = 0
    
    self
  end
   
  #   strio.string = string  -> string
  #
  # Changes underlying String object, the subject of IO.
  #
  def string=(str)
    @string = str.to_str
    @pos = 0
    @lineno = 0
  end 
  
  #   strio.rewind    -> 0
  #
  # Positions *strio* to the beginning of input, resetting
  # +lineno+ to zero.
  #
  def rewind
    @pos = 0
    @lineno = 0
  end
  
  #   strio.read([length [, buffer]])    -> string, buffer, or nil
  #
  # See IO#read.
  #
  def read(length = nil, buffer = ByteString.new)
    raise IOError, "not opened for reading" unless @readable
    raise TypeError unless buffer.respond_to?(:to_str)
    buffer = buffer.to_str      
 
    if length == nil
      return "" if self.eof?
      buffer.replace(@string[@pos..-1])
      @pos = string.size
    else
      return nil if self.eof?
      raise TypeError unless length.respond_to?(:to_int)
      length = length.to_int       
      raise ArgumentError if length < 0
      buffer.replace(string[pos, length])
      @pos += buffer.length
    end
 
    buffer
  end
  
  #   strio.sysread(integer[, outbuf])    -> string
  #
  # Similar to #read, but raises +EOFError+ at end of string instead of
  # returning +nil+, as well as IO#sysread does.
  def sysread(length = nil, buffer = ByteString.new)
    val = read(length, buffer)
    ( buffer.clear && raise(IO::EOFError, "end of file reached")) if val == nil
    val
  end  
  alias_method :readpartial, :sysread
  
  #   strio.readbyte   -> fixnum
  #
  # See IO#readbyte.
  #
  def readbyte
    raise(IO::EOFError, "end of file reached") if eof?
    getbyte
  end
  
  #   strio.seek(amount, whence=SEEK_SET) -> 0
  #
  # Seeks to a given offset _amount_ in the stream according to
  # the value of _whence_ (see IO#seek).
  #
  def seek(offset, whence = ::IO::SEEK_SET) 
    raise(IOError, "closed stream") if closed?
    raise TypeError unless offset.respond_to?(:to_int)       
    offset = offset.to_int
    
    case whence
    when ::IO::SEEK_CUR
      # Seeks to offset plus current position
      offset += @pos
    when ::IO::SEEK_END
      # Seeks to offet plus end of stream (usually offset is a negative value)
      offset += @string.size
    when ::IO::SEEK_SET, nil
      # Seeks to the absolute location given by offset
    else
      raise Errno::EINVAL, "invalid whence"
    end
    
    raise Errno::EINVAL if (offset < 0)
    @pos = offset  
    
    0
  end 
  
  #   strio.pos = integer    -> integer
  #
  # Seeks to the given position (in bytes) in *strio*.
  #
  def pos=(pos)
    raise Errno::EINVAL if pos < 0
    @pos = pos
  end
  
  #   strio.closed?    -> true or false
  #
  # Returns +true+ if *strio* is completely closed, +false+ otherwise.
  #
  def closed?
    !@readable && !@writable
  end 
  
  #   strio.close  -> nil
  #
  # Closes strio.  The *strio* is unavailable for any further data 
  # operations; an +IOError+ is raised if such an attempt is made.
  #
  def close
    raise(IOError, "closed stream") if closed?
    @readable = @writable = nil
  end
  
  #   strio.closed_read?    -> true or false
  #
  # Returns +true+ if *strio* is not readable, +false+ otherwise.
  #
  def closed_read?
    !@readable
  end
  
  #   strio.close_read    -> nil
  #
  # Closes the read end of a StringIO.  Will raise an +IOError+ if the
  # *strio* is not readable.
  #
  def close_read
    raise(IOError, "closing non-duplex IO for reading") unless @readable
    @readable = nil
  end
  
  #   strio.closed_write?    -> true or false
  #
  # Returns +true+ if *strio* is not writable, +false+ otherwise.
  #
  def closed_write?
    !@writable
  end
  
  #   strio.eof     -> true or false
  #   strio.eof?    -> true or false
  #
  # Returns true if *strio* is at end of file. The stringio must be  
  # opened for reading or an +IOError+ will be raised.
  #
  def eof?
    raise(IOError, "not opened for reading") unless @readable
    pos >= @string.length
  end
  alias_method :eof, :eof? 
  
  def binmode
    self
  end
  
  def fcntl
    raise NotImplementedError, "StringIO#fcntl is not implemented"
  end
  
  def flush
    self
  end
  
  def fsync
    0
  end
  
  # strio.path -> nil
  #
  def path
    nil
  end
  
  # strio.pid -> nil
  # 
  def pid
    nil
  end
  
  #   strio.sync    -> true
  #
  # Returns +true+ always.
  #
  def sync
    true
  end
  
  #    strio.sync = boolean -> boolean
  #
  def sync=(value)
    value
  end
  
  def tell
    @pos
  end
  
  # strio.fileno -> nil
  #
  def fileno
    nil
  end
  
  #   strio.isatty -> nil
  #   strio.tty? -> nil
  def isatty
    false
  end
  alias_method :tty?, :isatty
  
  def length
    string.length
  end
  alias_method :size, :length 
  
  #   strio.getc   -> string or nil
  #
  # Gets the next character from io.
  # Returns nil if called at end of ﬁle
  def getc
    return nil if eof?
    result = string[pos]
    @pos += 1
    result
  end
    
  #   strio.ungetc(string)   -> nil
  #
  # Pushes back one character (passed as a parameter) onto *strio*
  # such that a subsequent buffered read will return it.  Pushing back 
  # behind the beginning of the buffer string is not possible.  Nothing
  # will be done if such an attempt is made.
  # In other case, there is no limitation for multiple pushbacks.
  #
  def ungetc(chars)
    raise(IOError, "not opened for reading") unless @readable
    raise TypeError unless chars.respond_to?(:to_str)       
    chars = chars.to_str
    
    if pos == 0
      @string = chars + string    
    elsif pos > 0
      @pos -= 1
      string[pos] = chars      
    end    
        
    nil
  end
  
  #   strio.readchar   -> fixnum
  #
  # See IO#readchar.
  #
  def readchar
    raise(IO::EOFError, "end of file reached") if eof?
    getc
  end
  
  #   strio.each_char {|char| block }  -> strio
  #
  # See IO#each_char.
  #
  def each_char
    raise(IOError, "not opened for reading") unless @readable
    if block_given?
      string.each_char{|c| yield(c)}
      self
    else
      string.each_char
    end
  end
  alias_method :chars, :each_char 
  
  #   strio.getbyte   -> fixnum or nil
  #
  # See IO#getbyte.
  def getbyte
    raise(IOError, "not opened for reading") unless @readable
    # Because we currently don't support bytes access
    # the following code isn't used
    # instead we are dealing with chars
    result = string.bytes.to_a[pos]
    @pos += 1 unless eof?
    result 
    # getc
  end
  
  #   strio.each_byte {|byte| block }  -> strio
  #
  # See IO#each_byte.
  #
  def each_byte
    raise(IOError, "not opened for reading") unless @readable
    return self if (pos > string.length)
    if block_given?
      string.each_byte{|b| @pos += 1; yield(b)}
      self
    else
      string.each_byte
    end
  end
  alias_method :bytes, :each_byte 
  
  
  #   strio.each(sep=$/) {|line| block }         -> strio
  #   strio.each(limit) {|line| block }          -> strio
  #   strio.each(sep, limit) {|line| block }     -> strio
  #   strio.each_line(sep=$/) {|line| block }    -> strio
  #   strio.each_line(limit) {|line| block }     -> strio
  #   strio.each_line(sep,limit) {|line| block } -> strio
  #
  # See IO#each.
  #
  def each(sep = $/)
    if block_given?
      raise(IOError, "not opened for reading") unless @readable
      sep = sep.to_str unless sep == nil
      while line = getline(sep)
        yield(line)
      end
      self
    else
      to_enum(:each, sep)
    end
  end 
  alias_method :each_line, :each
  alias_method :lines, :each
  
  #   strio.gets(sep=$/)     -> string or nil
  #   strio.gets(limit)      -> string or nil
  #   strio.gets(sep, limit) -> string or nil
  #
  # See IO#gets.
  #
  def gets(sep=$/)
    $_ = getline(sep)
  end
  
  #   strio.readline(sep=$/)     -> string
  #   strio.readline(limit)      -> string or nil
  #   strio.readline(sep, limit) -> string or nil
  #
  # See IO#readline.
  def readline(sep=$/)
    raise(IO::EOFError, 'end of file reached') if eof?
    $_ = getline(sep)
  end
  
  #   strio.readlines(sep=$/)    ->   array
  #   strio.readlines(limit)     ->   array
  #   strio.readlines(sep,limit) ->   array
  #
  # See IO#readlines.
  #
  def readlines(sep=$/)
    raise IOError, "not opened for reading" unless @readable
    ary = []
    while line = getline(sep)
      ary << line
    end
    ary
  end 
  
  
  #   strio.write(string)    -> integer
  #   strio.syswrite(string) -> integer
  #
  # Appends the given string to the underlying buffer string of *strio*.
  # The stream must be opened for writing.  If the argument is not a
  # string, it will be converted to a string using <code>to_s</code>.
  # Returns the number of bytes written.  See IO#write.
  #
  def write(str)
    str = str.to_s
    return 0 if str.empty?

    raise(IOError, "not opened for writing") unless @writable    
 
    if @append || (pos >= string.length)
      # add padding in case it's needed 
      str = str.rjust((pos + str.length) - string.length, "\000") if (pos > string.length)
      @string << str
      @pos = string.length
    else
      @string[pos, str.length] = str
      @pos += str.length
      @string.taint if str.tainted?
    end
 
    str.length
  end
  alias_method :syswrite, :write
  
  #   strio << obj     -> strio
  #
  # See IO#<<.
  #
  def <<(str)
    self.write(str)
    self
  end

  
  def close_write
    raise(IOError, "closing non-duplex IO for writing") unless @writable
    @writable = nil
  end
  
  #   strio.truncate(integer)    -> 0
  #
  # Truncates the buffer string to at most _integer_ bytes. The *strio*
  # must be opened for writing.
  #
  def truncate(len)
    raise(IOError, "closing non-duplex IO for writing") unless @writable
    raise(TypeError) unless len.respond_to?(:to_int)
    length = len.to_int
    raise(Errno::EINVAL, "negative length") if (length < 0)
    if length < string.size
      @string[length .. string.size] = ""
    else
      @string = string.ljust(length, "\000")
    end
    # send back what was passed, not our :to_int version
    len 
  end
  
  #   strio.puts(obj, ...)    -> nil
  #
  #  Writes the given objects to <em>strio</em> as with
  #  <code>IO#print</code>. Writes a record separator (typically a
  #  newline) after any that do not already end with a newline sequence.
  #  If called with an array argument, writes each element on a new line.
  #  If called without arguments, outputs a single record separator.
  #
  #     io.puts("this", "is", "a", "test")
  #
  #  <em>produces:</em>
  #
  #     this
  #     is
  #     a
  #     test
  #
  def puts(*args)
    if args.empty?
      write("\n")
    else
      args.each do |arg|
        if arg == nil
          line = "nil"
        else
          begin
            arg = arg.to_ary
            arg.each {|a| puts a }
            next
          rescue
            line = arg.to_s
          end
        end 
        
        write(line)
        write("\n") if !line.empty? && (line[-1] != ?\n)
      end
    end
    
    nil
  end
  
  
  #   strio.putc(obj)    -> obj
  #
  #  If <i>obj</i> is <code>Numeric</code>, write the character whose
  #  code is <i>obj</i>, otherwise write the first character of the
  #  string representation of  <i>obj</i> to <em>strio</em>.
  #
  def putc(obj)
    raise(IOError, "not opened for writing") unless @writable

    if obj.is_a?(String)
      char = obj[0]
    else
      raise TypeError unless obj.respond_to?(:to_int)  
      char = obj.to_int
    end

    if @append || pos == string.length
      @string << char
      @pos = string.length
    elsif pos > string.length
      @string = string.ljust(pos, "\000")
      @string << char
      @pos = string.length
    else
      @string[pos] = ("" << char)
      @pos += 1
    end

    obj
  end
    
  
  #     strio.print()             => nil
  #     strio.print(obj, ...)     => nil
  #
  #  Writes the given object(s) to <em>strio</em>. The stream must be
  #  opened for writing. If the output record separator (<code>$\\</code>)
  #  is not <code>nil</code>, it will be appended to the output. If no
  #  arguments are given, prints <code>$_</code>. Objects that aren't
  #  strings will be converted by calling their <code>to_s</code> method.
  #  With no argument, prints the contents of the variable <code>$_</code>.
  #  Returns <code>nil</code>.
  #
  #     io.print("This is ", 100, " percent.\n")
  #
  #  <em>produces:</em>
  #
  #     This is 100 percent.
  #
  def print(*args)
    raise IOError, "not opened for writing" unless @writable
    args << $_ if args.empty?
    args.map! { |x| (x == nil) ? "nil" : x }
    write((args << $\).flatten.join)
    nil
  end 
  
  #     printf(strio, string [, obj ... ] )    => nil
  #
  #  Equivalent to:
  #     strio.write(sprintf(string, obj, ...)
  #
  def printf(*args)
    raise IOError, "not opened for writing" unless @writable

    if args.size > 1
      write(args.shift % args)
    else
      write(args.first)
    end

    nil
  end          


  protected
    
    # meant to be overwritten by developers
    def to_strio
      self
    end
    
    def define_mode(mode=nil)
      if mode == nil
        # default modes
        string.frozen? ? set_mode_from_string("r") : set_mode_from_string("r+") 
      elsif mode.is_a?(Integer)
        set_mode_from_integer(mode)
      else
        mode = mode.to_str
        set_mode_from_string(mode)
      end 
    end   

    def set_mode_from_string(mode)
      @readable = @writable = @append = false

      case mode
      when "r", "rb"
        @readable = true
      when "r+", "rb+"
        raise(Errno::EACCES) if string.frozen?
        @readable = true
        @writable = true
      when "w", "wb"
        string.frozen? ? raise(Errno::EACCES) : string.replace("")
        @writable = true
      when "w+", "wb+"
        string.frozen? ? raise(Errno::EACCES) : string.replace("")
        @readable = true
        @writable = true
      when "a", "ab"
        raise(Errno::EACCES) if string.frozen?
        @writable = true
        @append = true
      when "a+", "ab+"
        raise(Errno::EACCES) if string.frozen?
        @readable = true
        @writable = true
        @append = true
      end
    end
    
    def set_mode_from_integer(mode)
      @readable = @writable = @append = false
 
      case mode & (IO::RDONLY | IO::WRONLY | IO::RDWR)
      when IO::RDONLY
        @readable = true
        @writable = false
      when IO::WRONLY
        @readable = false
        @writable = true
        raise(Errno::EACCES) if string.frozen?
      when IO::RDWR
        @readable = true
        @writable = true
        raise(Errno::EACCES) if string.frozen?
      end
 
      @append = true if (mode & IO::APPEND) != 0
      raise(Errno::EACCES) if @append && string.frozen?
      @string.replace("") if (mode & IO::TRUNC) != 0
    end
    
    def getline(sep = $/)
      raise(IOError, "not opened for reading") unless @readable
      return nil if eof?
      sep = sep.to_str unless (sep == nil)
      
      if sep == nil
        line = string[pos .. -1]
        @pos = string.size
      elsif sep.empty?
        if stop = string.index("\n\n", pos)
          stop += 1
          line = string[pos .. stop]
          while string[stop] == ?\n
            stop += 1
          end  
          @pos = stop
        else
          line = string[pos .. -1]
          @pos = string.size
        end
      else
        if stop = string.index(sep, pos)
          line = string[pos .. stop]
          @pos = stop + 1
        else
          line = string[pos .. -1]
          @pos = string.size
        end
      end

      @lineno += 1

      line
    end  

end
