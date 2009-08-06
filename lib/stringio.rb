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

  # StringIO.new(string=""[, mode])
  #
  # Creates new StringIO instance from with _string_ and _mode_.
  #
  def initialize(string = "", mode = nil)
    @string = string.kind_of?(String) ? string : string.to_str  
    @pos = 0
    @lineno = 0
    define_mode(mode)
    
    raise Errno::EACCES if (@writable && string.frozen?)   
    self
  end
  
  def initialize_copy(from)
    from = from.to_strio unless from.kind_of?(StringIO)
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
  def reopen(string, mode = nil)
    self.taint if string.tainted?
    if !string.kind_of?(String) && mode.nil?
      @string = string.to_strio.string 
    else
      @string = string  
    end
    
    define_mode(mode)
    @pos = 0
    @lineno = 0
    
    self
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
  def read(length = nil, buffer = "")
    raise IOError, "not opened for reading" unless @readable
    
    unless buffer.kind_of?(String)
      begin
        buffer = buffer.to_str
      rescue NoMethodError
        raise TypeError
      end
    end 
 
    if length.nil?
      return "" if self.eof?
      buffer.replace(@string[@pos..-1])
      @pos = @string.size
    else
      return nil if self.eof?
      unless length.kind_of?(Integer)
        begin
          length = length.to_int
        rescue
          raise TypeError
        end        
      end
      raise ArgumentError if length < 0
      buffer.replace(@string[@pos, length])
      @pos += buffer.length
    end
 
    buffer
  end
  
  #   strio.seek(amount, whence=SEEK_SET) -> 0
  #
  # Seeks to a given offset _amount_ in the stream according to
  # the value of _whence_ (see IO#seek).
  #
  def seek(offset, whence = ::IO::SEEK_SET) 
    raise(IOError, "closed stream") if closed?
    unless offset.kind_of?(Integer)
      raise TypeError unless offset.respond_to?(:to_int)
      offset = offset.to_int
    end
    
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
    raise IOError, "closing non-duplex IO for reading" unless @readable
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
  
  #   strio.getc   -> string or nil
  #
  # Gets the next character from io.
  # Returns nil if called at end of ﬁle
  def getc
    return nil if eof?
    @pos += 1
    @string[@pos]
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
    unless chars.kind_of?(Integer)
      raise TypeError unless chars.respond_to?(:to_str)
      chars = chars.to_str
    end
    
    if pos == 0
      @string = chars + @string    
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
    raise(IO::EOFError, "end of file reached") if self.eof?
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
  def get_byte
  end
  
  #   strio.each_byte {|byte| block }  -> strio
  #
  # See IO#each_byte.
  #
  def each_byte
  end
  
  #   strio.gets(sep=$/)     -> string or nil
  #   strio.gets(limit)      -> string or nil
  #   strio.gets(sep, limit) -> string or nil
  #
  # See IO#gets.
  #
  def gets(sep=$/)
    $_ = self.getline(sep)
  end 
           


  protected 
  
    def finalize
      self.close
      @string = nil
      self
    end
    
    def define_mode(mode=nil)
      if mode.nil?
        # default modes
        string.frozen? ? set_mode_from_string("r") : set_mode_from_string("r+") 
      else
        if mode.is_a?(Integer)
          set_mode_from_integer(mode)
        else
          mode = mode.to_str
          set_mode_from_string(mode)
        end
      end 
    end   

    def set_mode_from_string(mode)
      @readable = @writable = @append = false

      case mode
      when "r", "rb"
        @readable = true
      when "r+", "rb+"
        @readable = true
        @writable = true
      when "w", "wb"
        string.frozen? ? raise(Errno::EACCES) : @string.replace("")
        @writable = true
      when "w+", "wb+"
        @readable = true
        @writable = true
        string.frozen? ? raise(Errno::EACCES) : @string.replace("")
      when "a", "ab"
        @writable = true
        @append = true
      when "a+", "ab+"
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
      when IO::RDWR
        @readable = true
        @writable = true
      end
 
      @append = true if (mode & IO::APPEND) != 0
      @string.replace("") if (mode & IO::TRUNC) != 0
    end
    
    def getline(sep = $/)
      raise(IOError, "not opened for reading") unless @readable
      sep = sep.to_str unless (sep.nil? || sep.kind_of?(String))
      return nil if self.eof?

      if sep.nil?
        line = string[pos .. -1]
        @pos = string.size
      elsif sep.empty?
        if stop = string.index("\n\n", pos)
          stop += 2
          line = string[pos .. (stop - 2)]
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

      return line
    end  

end