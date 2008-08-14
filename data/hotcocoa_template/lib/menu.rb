module HotCocoa
  def application_menu(app)
    menu do |main|
      main.submenu :apple do |apple|
        apple.item :about, :title => "About #{app.name}", :target => app, :action => "orderFrontStandardAboutPanel:"
        apple.separator
        apple.item :preferences, :key => ","
        apple.separator
        apple.submenu :services
        apple.separator
        apple.item :hide, :title => "Hide #{app.name}", :key => "h", :target => app, :action => "hide:"
        apple.item :hide_others, :title => "Hide Others", :key => "h", :modifiers => [:command, :alt],  :target => app, :action => "hideOtherApplications:"
        apple.item :show_all, :title => "Show All", :target => app, :action => "unhideAllApplications:"
        apple.separator
        apple.item :quit, :title => "Quit #{app.name}", :key => "q", :target => app, :action => "terminate:"
      end
      main.submenu :file do |file|
        file.item :new, :key => "n"
        file.item :open, :key => "o"
      end
      main.submenu :window do |win|
        win.item :minimize, :key => "m"
        win.item :zoom
        win.separator
        win.item :bring_all_to_front, :title => "Bring All to Front", :key => "o"
      end
      main.submenu :help do |help|
        help.item :help, :title => "#{app.name} Help"
      end
    end
  end
end
