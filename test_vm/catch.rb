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
