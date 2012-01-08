if $extout
  extout = "#$extout"
  install?(:ext, :arch, :'ext-arch') do
    puts "installing extension objects"
    makedirs [archlibdir, sitearchlibdir, vendorarchlibdir, archhdrdir]
    if noinst = CONFIG["no_install_files"] and noinst.empty?
      noinst = nil
    end
    install_recursive("#{extout}/#{CONFIG['arch']}", archlibdir, :no_install => noinst, :mode => $prog_mode)
    install_recursive("#{extout}/include/#{CONFIG['arch']}", archhdrdir, :glob => "*.h", :mode => $data_mode)
  end
  install?(:ext, :comm, :'ext-comm') do
    puts "installing extension scripts"
    hdrdir = rubyhdrdir + "/ruby"
    makedirs [rubylibdir, sitelibdir, vendorlibdir, hdrdir]
    install_recursive("#{extout}/common", rubylibdir, :mode => $data_mode)
    install_recursive("#{extout}/include/ruby", hdrdir, :glob => "*.h", :mode => $data_mode)
  end
end
