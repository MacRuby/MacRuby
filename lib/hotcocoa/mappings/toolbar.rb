HotCocoa::Mappings.map :toolbar => :NSToolbar do

  defaults :identifier => 'DefaultToolbarIdentifier',
           :allowed => [:separator, :space, :flexible_space, :show_colors,
                        :show_fonts, :customize, :print],
           :default => [],
           :allow_customization => true

  def init_with_options(toolbar, options)
    toolbar.initWithIdentifier options.delete(:identifier)

    allowed = options.delete(:allowed).dup
    default = options.delete(:default).dup

    ary = default.select { |x| x.is_a?(NSToolbarItem) }
    default -= ary
    custom_items = {}
    ary.each { |x| custom_items[x.itemIdentifier] = x } 
    allowed.concat(custom_items.keys)
    default.concat(custom_items.keys)

    [allowed, default].each do |a|
      a.map! do |i|
        case i
          when :separator
            NSToolbarSeparatorItemIdentifier
          when :space
            NSToolbarSpaceItemIdentifier
          when :flexible_space
            NSToolbarFlexibleSpaceItemIdentifier
          when :show_colors
            NSToolbarShowColorsItemIdentifier
          when :show_fonts
            NSToolbarShowFontsItemIdentifier
          when :customize
            NSToolbarCustomizeToolbarItemIdentifier
          when :print
            NSToolbarPrintItemIdentifier
          else
            i
        end
      end 
    end 
    o = Object.new
    o.instance_variable_set(:@allowed, allowed)
    o.instance_variable_set(:@default, default)
    o.instance_variable_set(:@custom_items, custom_items)
    def o.toolbarAllowedItemIdentifiers(sender); @allowed; end
    def o.toolbarDefaultItemIdentifiers(sender); @default; end
    def o.toolbar(sender, itemForItemIdentifier:identifier, 
                  willBeInsertedIntoToolbar:flag)
      @custom_items[identifier]
    end
    toolbar.delegate = o
    toolbar.allowsUserCustomization = options.delete(:allow_customization)
    toolbar
  end

end
