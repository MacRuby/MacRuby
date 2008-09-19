HotCocoa::Mappings.map :toolbar => :NSToolbar do

  constant :size, {
    :default  => NSToolbarSizeModeDefault,
    :regular  => NSToolbarSizeModeRegular,
    :small    => NSToolbarSizeModeSmall
  }

  constant :display, {
    :default        => NSToolbarDisplayModeDefault,
    :icon_and_label => NSToolbarDisplayModeIconAndLabel,
    :icon           => NSToolbarDisplayModeIconOnly,
    :label          => NSToolbarDisplayModeLabelOnly
  }

  defaults :identifier => 'DefaultToolbarIdentifier',
           :allowed => [:separator, :space, :flexible_space, :show_colors,
                        :show_fonts, :customize, :print],
           :default => [],
           :allow_customization => true,
           :size => :default

  def init_with_options(toolbar, options)
    toolbar = toolbar.initWithIdentifier options.delete(:identifier)
    toolbar.allowsUserCustomization = options.delete(:allow_customization)
    toolbar
  end
  
  custom_methods do

    def size=(mode)
      setSizeMode(mode)
    end

    def display=(mode)
      setDisplayMode(mode)
    end
    
    def default=(list)
      @default = list.dup
      build_custom_items
    end
    
    def allowed=(list)
      @allowed = list.dup
      build_custom_items
    end
    
    private
    
      def build_custom_items
        if @allowed && @default
          ary = @default.select { |x| x.is_a?(NSToolbarItem) }
          @default -= ary
          @custom_items = {}
          ary.each { |x| @custom_items[x.itemIdentifier] = x } 
          @allowed.concat(@custom_items.keys)
          @default.concat(@custom_items.keys)

          [@allowed, @default].each do |a|
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
          allowed_items { @allowed }
          default_items { @default }
          item_for_identifier { |identifier, will_be_inserted| @custom_items[identifier] }
        end
      end
    
  end

  delegating "toolbar:itemForItemIdentifier:willBeInsertedIntoToolbar:", :to => :item_for_identifier,         :parameters => [:itemForItemIdentifier, :willBeInsertedIntoToolbar], :required => true
  delegating "toolbarAllowedItemIdentifiers:",                           :to => :allowed_items, :required => true
  delegating "toolbarDefaultItemIdentifiers:",                           :to => :default_items, :required => true
  delegating "toolbarSelectableItemIdentifiers:",                        :to => :selectable_items
  delegating "toolbarDidRemoveItem:",                                    :to => :did_remove_item,             :parameters => ["toolbarDidRemoveItem.userInfo['item']"]
  delegating "toolbarWillAddItem:",                                      :to => :will_add_item,               :parameters => ["toolbarWillAddItem.userInfo['item']"]

end
