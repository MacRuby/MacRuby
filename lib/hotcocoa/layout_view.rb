module HotCocoa
class LayoutView < NSView

  def initWithFrame(frame)
    super
    @mode = :vertical
    @spacing = 0.0
    @margin = 0.0
    @options = {}
    self
  end

  def vertical?
    @mode == :vertical
  end

  def horizontal?
    @mode == :horizonal
  end

  def mode=(mode)
    if mode != :horizontal and mode != :vertical
      raise ArgumentError, "invalid mode value #{mode}"
    end
    if mode != @mode
      @mode = mode
      relayout!
    end
  end

  def spacing
    @spacing
  end

  def spacing=(spacing)
    if spacing != @spacing
      @spacing = spacing.to_i
      relayout!
    end
  end
 
  def margin
    @margin
  end

  def margin=(margin)
    if margin != @margin
      @margin = margin.to_i
      relayout!
    end
  end
  
  # options can be
  #
  #  :start -> bool
  #    Whether the view is packed at the start or the end of the packing view.
  #    Default value is true.
  #
  #  :expand -> bool
  #    Whether the view's first dimension (width for horizontal and height for vertical)
  #    should be expanded to the maximum possible size, and should be variable according
  #    to the packing view frame.
  #    Default value is false.
  #
  #  :padding         -> float
  #  :left_padding    -> float
  #  :right_padding   -> float
  #  :top_padding     -> float
  #  :bottom_padding  -> float
  #    Controls the padding area around the view. :padding controls all areas, while
  #    :left_padding for example only controls the left side. If :padding is set, other
  #    padding flags are ignored.
  #    Default value is 0.0 for all flags.
  #
  #  :other -> mode
  #    Controls the view's second dimension (height for horizontal and width for vertical).
  #    Modes can be:
  #      :align_head
  #        Will be aligned to the head (start) of the packing area.
  #      :align_center
  #        Will be centered inside the packing area.
  #      :align_tail
  #        Will be aligned to the tail (end) of the packing area.
  #      :fill
  #        Will be filled to the maximum size.
  def pack(view, options={})
    if subviews.include?(view)
      raise ArgumentError, "view #{view} already packed"
    end
    options[:start] = true unless options.has_key?(:start)
    options[:expand] = false unless options.has_key?(:expand)
    [:left_padding, :right_padding, :top_padding, :bottom_padding].each { |s| options[s] = (options[:padding] or 0.0) }
    options[:other] ||= :align_head
    @options[view] = options
    addSubview(view)
    relayout!
  end

  def change_option_for_view(view, key, value)
    old = @options[view][key]
    if old != value
      @options[view][key] = value
      relayout!
    end
  end

  def options_for_view(view)
    @options[view]
  end

  def unpack(view)
    unless subviews.include?(view)
      raise ArgumentError, "view #{view} not packed"
    end
    view.removeFromSuperview
    @options.delete(view)
  end

  def unpack_all_views
    subviews.each { |view| view.removeFromSuperview }
    @options.clear
  end

  if $DEBUG
    def drawRect(frame)
      NSColor.redColor.set
      NSFrameRect(frame)
    end
  end 

  def setFrame(frame)
    super
    relayout!
  end

  def setFrameSize(size)
    super
    relayout!
  end

  private

  def relayout!
    vertical = @mode == :vertical
    view_size = frameSize    
    dimension = @margin
    end_dimension = vertical ? view_size.height : view_size.width
    end_dimension -= (@margin * 2)

    expandable_size = end_dimension
    expandable_views = 0
    subviews.each do |view|
      options = @options[view]
      if options[:expand]
        expandable_views += 1
      else
        expandable_size -= vertical ? view.frameSize.height : view.frameSize.width
        expandable_size -= @spacing
      end
    end
    expandable_size /= expandable_views

    subviews.each do |view|
      options = @options[view]
      subview_size = view.frameSize
      view_frame = NSMakeRect(0, 0, *subview_size)
      subview_dimension = vertical ? subview_size.height : subview_size.width

      if vertical
        view_frame.origin.x = @margin
        if options[:start]
          view_frame.origin.y = dimension
        else
          view_frame.origin.y = end_dimension - subview_dimension
        end        
      else
        if options[:start]
          view_frame.origin.x = dimension
        else
          view_frame.origin.x = end_dimension - subview_dimension
        end        
        view_frame.origin.y = @margin
      end

      if options[:expand]
        if vertical
          view_frame.size.height = expandable_size
        else
          view_frame.size.width = expandable_size
        end
        subview_dimension = expandable_size
      end

      case options[:other]
      when :fill
        if vertical
          view_frame.size.width = view_size.width - (2 * @margin)
        else
          view_frame.size.height = view_size.height - (2 * @margin)
        end           

      when :align_head
        # Nothing to do

      when :align_center
        if vertical
          view_frame.origin.x = (view_size.width / 2.0) - (subview_size.width / 2.0)
        else
          view_frame.origin.y = (view_size.height / 2.0) - (subview_size.height / 2.0)
        end

      when :align_tail
        if vertical
          view_frame.origin.x = view_size.width - subview_size.width - @margin
        else
          view_frame.origin.y = view_size.height - subview_size.height - @margin
        end
      end

      puts "view #{view} options #{options} final frame #{view_frame}" if $DEBUG

      view_frame.origin.x += options[:left_padding]
      view_frame.origin.y += options[:bottom_padding]

      if options[:start]
        dimension += subview_dimension + @spacing
        if vertical
          dimension += options[:bottom_padding] + options[:top_padding]
        else
          dimension += options[:left_padding] + options[:right_padding]
        end
      else
        end_dimension -= subview_dimension + @spacing
      end

      view.frame = view_frame      
    end    
  end
end
end
