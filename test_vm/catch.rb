assert ':ok', %{
  catch(:x) do
    begin
      catch(:x) do
        raise ''
      end
    rescue
    end
    throw :x
    p :ko
  end
  p :ok
}

assert ':ok', %{
  def foo
    catch(:x) do
      1.times { return }
    end
    p :ko
  end
  catch(:x) do foo(); throw :x; end
  p :ok
}

assert '', %{
  catch(:x) do
    begin
      begin
        raise ''
      rescue Exception => e
        throw :x
      end
    rescue Exception => e
      p :ko
    end
  end
}

assert '', %{
  def foo
    begin
      raise ''
    rescue
      raise
    end
  end

  4.times {|fn| foo rescue nil }

  catch(:enddoc) do
    begin
      throw :enddoc
    rescue Exception => e
      p :ko
    end
  end
}

assert ':ok', %{
  begin  
    def foo
      raise Exception, catch(:x) { throw :x }
    end

    begin
      raise
    rescue
      foo
    end
  rescue Exception
    p :ok
  end
}

assert ':ok', %{
  catch(:x) do
    begin
      raise
    rescue
      catch(:x) { throw :x }
    end
  end
  p :ok
}
