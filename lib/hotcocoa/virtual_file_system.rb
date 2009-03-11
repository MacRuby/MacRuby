class VirtualFileSystem
  
  def self.code_to_load(main_file, password = "\320\302\354\024\215\360P\235\2244\261\214\276:'\274%\270<\350t\212\003\340\t'\004\345\334\256\315\277")
    <<-VFS
    module DatabaseRuntime

      class VirtualFilesystem

        attr_reader :password, :encryption_method

        def initialize(password, encryption_method="aes256")
          @password = password
          @encryption_method = encryption_method
        end

        def open_database(main_database)
          @main_database = main_database
          begin
            @db = Marshal.load(File.open(main_database, "rb") {|f| f.read})
          rescue 
            puts $!
            puts $!.backtrace.join("\\n")
          end
        end

        def read_file_and_key(filename)
          data, key = read_file_using_path(filename)
          if data.nil? && filename[0,1] != "/"
            $:.each do |loadpath|
              data, key = read_file_using_path(filename, expand_path(loadpath))
              break if data
            end
          end
          [data, key]
        end

        def read_file_using_path(filename, pathname="/")
          path = expand_path(File.join(pathname, filename))
          rb_ext = false
          if has_key?(path)
            key = filename
          elsif self.has_key?(path+".rb")
            key = filename+".rb"
            rb_ext = true
          end
          if key
            result = rb_ext ? self[path+".rb"] : self[path]
            if result
              result.gsub!(/__FILE__/, "'\#{path + (rb_ext ? ".rb" : '')}'")
              return result, key
            end
          end
          nil
        end

        def has_key?(key)
          @db.has_key?(key)
        end

        def delete(key)
        end

        def [](key)
          read(@db, key)
        end

        def close
        end

        def keys
          @db.keys
        end

        def []=(key, value)
        end

        def normalize(filename)
          f = filename.split(File::Separator)
          f.delete(".")
          while f.include?("..")
            f[f.index("..")-1,2] = []
          end
          f.join(File::Separator)
        end

        private 

          def expand_path(filename, dirstring=nil)
            filename = File.join(dirstring, filename) if dirstring
            f = filename.split(File::Separator)
            f.delete(".")
            f.delete("")
            while f.include?("..")
              f[f.index("..")-1,2] = []
            end
            "/"+f.join(File::Separator)
          end


          def read(db, key)
            if db.has_key?(key)
              #decrypt = OpenSSL::Cipher::Cipher.new(encryption_method)
              #decrypt.decrypt
              #decrypt.key = password
              #data = decrypt.update(db[key])
              #data << decrypt.final
              #::Zlib::Inflate.inflate(data)
              db[key]
            else
              nil
            end
          end

      end

    end

    $ROOT_BINDING = binding

    module Kernel

      def virtual_filesystems(options)
        vfs = DatabaseRuntime::VirtualFilesystem.new(options["password"])
        vfs.open_database(options["database"])
        Kernel.const_set("VIRTUAL_FILESYSTEM", vfs)
        vfs
      end

      alias_method :original_require, :require

      def require(path)
        path = VIRTUAL_FILESYSTEM.normalize(path)
        path_loaded =  $".include?(path) || $".include?(path+".rb") || $".include?(path+".so")
        unless path_loaded
          data, key = VIRTUAL_FILESYSTEM.read_file_and_key(path)
          if data
            $" << key
            eval(data, $ROOT_BINDING, key, 0)
            return true
          else
            original_require(path)
          end
        else
          return false
        end
      end

      alias_method :original_load, :load

      def load(path)
        path = VIRTUAL_FILESYSTEM.normalize(path)
        data, key = VIRTUAL_FILESYSTEM.read_file_and_key(path)
        if data
          eval(data, $ROOT_BINDING, key, 0)
          return true
        else
          begin
            original_load(path)
          rescue LoadError => e
            raise e
          end
        end
      end

    end

    virtual_filesystems("database"=>File.join(NSBundle.mainBundle.resourcePath.fileSystemRepresentation, 'vfs.db'), "password"=>#{password.inspect})
    $:.unshift "/lib"
    load '#{main_file}'
    VFS
  end
end