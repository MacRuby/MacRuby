module HotCocoa
  
module LayoutManaged
  
  def layout=(options)
    @layout = LayoutOptions.new(self, options)
    @layout.update_layout_views!
  end
  
  def layout
    @layout
  end
  
end

class LayoutOptions

  VALID_EXPANSIONS = [nil, :height, :width, [:height, :width], [:width, :height]]
  
  attr_accessor :defaults_view
  attr_reader   :view
  
  # options can be
  #
  #  :start -> bool
  #    Whether the view is packed at the start or the end of the packing view.
  #    Default value is true.
  #
  #  :expand ->  :height, :width, [:height, :width]
  #    Whether the view's first dimension (width for horizontal and height for vertical)
  #    should be expanded to the maximum possible size, and should be variable according
  #    to the packing view frame.
  #    Default value is nil.
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
  #  :align -> mode
  #    Controls the view's alignment if its not expanded in the other dimension
  #    Modes can be:
  #      :left
  #        For horizontal layouts, align left
  #      :center
  #        Align center for horizontal or vertical layouts
  #      :right
  #        For horizontal layouts, align right
  #      :top
  #        For vertical layouts, align top
  #      :bottom
  #        For vertical layouts, align bottom
  def initialize(view, options={})
    @view = view
    @start          = options[:start]
    @expand         = options[:expand]
    @padding        = options[:padding]
    @left_padding   = @padding || options[:left_padding]
    @right_padding  = @padding || options[:right_padding]
    @top_padding    = @padding || options[:top_padding]
    @bottom_padding = @padding || options[:bottom_padding]
    @align          = options[:align]
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
    unless VALID_EXPANSIONS.include?(value)
      raise ArgumentError, "Expand must be nil, :height, :width or [:width, :height] not #{value.inspect}"
    end
    @expand = value
    update_layout_views!
  end
  
  def expand
    return @expand unless @expand.nil?
    if in_layout_view?
      @view.superview.default_layout.expand
    else
      false
    end
  end

  def expand_width?
    e = self.expand
    e == :width || (e.respond_to?(:include?) && e.include?(:width))
  end

  def expand_height?
    e = self.expand
    e == :height || (e.respond_to?(:include?) && e.include?(:height))
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
  
  def align
    return @align unless @align.nil?
    if in_layout_view?
      @view.superview.default_layout.align
    else
      :left
    end
  end

  def align=(value)
    return if value == @align
    @align = value
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
    "#<#{self.class} start=#{start?}, expand=#{expand.inspect}, left_padding=#{left_padding}, right_padding=#{right_padding}, top_padding=#{top_padding}, bottom_padding=#{bottom_padding}, align=#{align.inspect}, view=#{view.inspect}>"
  end

  def update_layout_views!
    @view.superview.views_updated! if in_layout_view?
    @defaults_view.views_updated! if @defaults_view
  end
  
  private
  
    def in_layout_view?
      @view && @view.superview.kind_of?(LayoutView)
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
  
  def frame=(frame)
    setFrame(frame)
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

  def remove(subview, options = {})
    raise ArgumentError, "#{subview} is not a subview of #{self} and cannot be removed." unless subview.superview == self
    options[:needs_display] == false ? subview.removeFromSuperviewWithoutNeedingDisplay : subview.removeFromSuperview
  end
    
  def addSubview(view)
    super
    if view.respond_to?(:layout)
      relayout!
    else
      raise ArgumentError, "view #{view} does not support the #layout method"
    end
  end
  
  def views_updated!
    relayout!
  end

  def remove_view(view)
    unless subviews.include?(view)
      raise ArgumentError, "view #{view} not a subview of this LayoutView"
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
      if (vertical ? view.layout.expand_height? : view.layout.expand_width?)
        expandable_views += 1
      else
        expandable_size -= vertical ? view.frameSize.height : view.frameSize.width
        expandable_size -= @spacing
      end
      expandable_size -= 
        vertical ? view.layout.top_padding + view.layout.bottom_padding 
                 : view.layout.left_padding + view.layout.right_padding
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

      if (vertical ? options.expand_height? : options.expand_width?)
        if vertical
          view_frame.size.height = expandable_size
        else
          view_frame.size.width = expandable_size
        end
        subview_dimension = expandable_size
      end
      
      if (vertical ? options.expand_width? : options.expand_height?)
        if vertical
          view_frame.size.width = view_size.width - (2 * @margin) - options.right_padding - options.left_padding
        else
          view_frame.size.height = view_size.height - (2 * @margin) - options.top_padding - options.bottom_padding
        end
      else
        case options.align
        when :left, :bottom
          # Nothing to do

        when :center
          if vertical
            view_frame.origin.x = (view_size.width / 2.0) - (subview_size.width / 2.0)
          else
            view_frame.origin.y = (view_size.height / 2.0) - (subview_size.height / 2.0)
          end

        when :right, :top
          if vertical
            view_frame.origin.x = view_size.width - subview_size.width - @margin
          else
            view_frame.origin.y = view_size.height - subview_size.height - @margin
          end
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
