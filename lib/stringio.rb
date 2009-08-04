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
    @string = string.to_str unless string.kind_of?(String) 
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
    @pos, @lineno = 0, 0
    
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
    pos >= string.length
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

end    