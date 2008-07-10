module HotCocoa

class LayoutOptions
  
  attr_accessor :defaults_view
  attr_reader   :view
  
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
  def initialize(view, options={})
    @view           = view
    @start          = options[:start]
    @expand         = options[:expand]
    @padding        = options[:padding]
    @left_padding   = options[:left_padding]    || @padding
    @right_padding  = options[:right_padding]   || @padding
    @top_padding    = options[:top_padding]     || @padding
    @bottom_padding = options[:bottom_padding]  || @padding
    @other          = options[:other]
    update_layout_views!
    @defaults_view  = options[:defaults_view]
  end

  def start=(value)
    return if value == @start
    @start = value
    update_layout_views!
  end
  
  def start?
    return @start unless @start.nil?
    if in_layout_view?
      @view.superview.default_layout.start?
    else
      true
    end
  end
  
  def expand=(value)
    return if value == @expand
    @expand = value
    update_layout_views!
  end
  
  def expand?
    return @expand unless @expand.nil?
    if in_layout_view?
      @view.superview.default_layout.expand?
    else
      false
    end
  end
  
  def left_padding=(value)
    return if value == @left_padding
    @left_padding = value
    @padding = nil
    update_layout_views!
  end
  
  def left_padding
    return @left_padding unless @left_padding.nil?
    if in_layout_view?
      @view.superview.default_layout.left_padding
    else
      @padding || 0.0
    end
  end

  def right_padding=(value)
    return if value == @right_padding
    @right_padding = value
    @padding = nil
    update_layout_views!
  end
  
  def right_padding
    return @right_padding unless @right_padding.nil?
    if in_layout_view?
      @view.superview.default_layout.right_padding
    else
      @padding || 0.0
    end
  end

  def top_padding=(value)
    return if value == @top_padding
    @top_padding = value
    @padding = nil
    update_layout_views!
  end

  def top_padding
    return @top_padding unless @top_padding.nil?
    if in_layout_view?
      @view.superview.default_layout.top_padding
    else
      @padding || 0.0
    end
  end

  def bottom_padding=(value)
    return if value == @bottom_padding
    @bottom_padding = value
    @padding = nil
    update_layout_views!
  end

  def bottom_padding
    return @bottom_padding unless @bottom_padding.nil?
    if in_layout_view?
      @view.superview.default_layout.bottom_padding
    else
      @padding || 0.0
    end
  end
  
  def other
    return @other unless @other.nil?
    if in_layout_view?
      @view.superview.default_layout.other
    else
      :align_head
    end
  end

  def other=(value)
    return if value == @other
    @other = value
    update_layout_views!
  end
  
  def padding=(value)
    return if value == @padding
    @right_padding = @left_padding = @top_padding = @bottom_padding = value
    @padding = value
    update_layout_views!
  end
  
  def padding
    @padding
  end
  
  def inspect
    "#<#{self.class} start=#{start?}, expand=#{expand?}, left_padding=#{left_padding}, right_padding=#{right_padding}, top_padding=#{top_padding}, bottom_padding=#{bottom_padding}, other=#{other.inspect}, view=#{view.inspect}>"
  end
  
  private
  
    def in_layout_view?
      @view && @view.superview.kind_of?(LayoutView)
    end

    def update_layout_views!
      @view.superview.views_updated! if in_layout_view?
      @defaults_view.views_updated! if @defaults_view
    end
    
end

class LayoutView < NSView
  
  def initWithFrame(frame)
    super
    @mode = :vertical
    @spacing = 10.0
    @margin = 10.0
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
  
  def default_layout=(options)
    options[:defaults_view] = self
    @default_layout = LayoutOptions.new(nil, options)
    relayout!
  end
  
  def default_layout
    @default_layout ||= LayoutOptions.new(nil, :defaults_view => self)
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
  
  def <<(view)
    addSubview(view)
  end
  
  def addSubview(view)
    super
    relayout! if view.respond_to?(:layout)
  end
  
  def views_updated!
    relayout!
  end

  def remove_view(view)
    unless subviews.include?(view)
      raise ArgumentError, "view #{view} not packed"
    end
    view.removeFromSuperview
    relayout!
  end

  def remove_all_views
    subviews.each { |view| view.removeFromSuperview }
    relayout!
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
      next if !view.respond_to?(:layout) || view.layout.nil?
      if view.layout.expand?
        expandable_views += 1
      else
        expandable_size -= vertical ? view.frameSize.height : view.frameSize.width
        expandable_size -= @spacing
      end
    end
    expandable_size /= expandable_views

    subviews.each do |view|
      next if !view.respond_to?(:layout) || view.layout.nil?
      options = view.layout
      subview_size = view.frameSize
      view_frame = NSMakeRect(0, 0, *subview_size)
      subview_dimension = vertical ? subview_size.height : subview_size.width

      if vertical
        view_frame.origin.x = @margin
        if options.start?
          view_frame.origin.y = dimension
        else
          view_frame.origin.y = end_dimension - subview_dimension
        end        
      else
        if options.start?
          view_frame.origin.x = dimension
        else
          view_frame.origin.x = end_dimension - subview_dimension
        end        
        view_frame.origin.y = @margin
      end

      if options.expand?
        if vertical
          view_frame.size.height = expandable_size
        else
          view_frame.size.width = expandable_size
        end
        subview_dimension = expandable_size
      end

      case options.other
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

      view_frame.origin.x += options.left_padding
      view_frame.origin.y += options.bottom_padding

      if options.start?
        dimension += subview_dimension + @spacing
        if vertical
          dimension += options.bottom_padding + options.top_padding
        else
          dimension += options.left_padding + options.right_padding
        end
      else
        end_dimension -= subview_dimension + @spacing
      end

      view.frame = view_frame      
    end    
  end
end
end
