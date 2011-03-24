=begin
= $RCSfile$ -- Ruby-space predefined Digest subclasses

= Info
  'OpenSSL for Ruby 2' project
  Copyright (C) 2002  Michal Rokos <m.rokos@sh.cvut.cz>
  All rights reserved.

= Licence
  This program is licenced under the same licence as Ruby.
  (See the file 'LICENCE'.)

= Version
  $Id: digest.rb 25189 2009-10-02 12:04:37Z akr $
=end

##
# Should we care what if somebody require this file directly?
#require 'openssl'

module OpenSSL
  class Digest

    alg = %w(DSS DSS1 MD2 MD4 MD5 MDC2 RIPEMD160 SHA SHA1)
    if OPENSSL_VERSION_NUMBER > 0x00908000
      alg += %w(SHA224 SHA256 SHA384 SHA512)
    end

    def self.digest(name, data)
        super(data, name)
    end

    class Stub < Digest
      class << self
        def digest(data)
          Digest.digest(@name, data)
        end

        def hexdigest(data)
          Digest.hexdigest(@name, data)
        end
      end

      def initialize(*data)
        if data.length > 1
          raise ArgumentError,
            "wrong number of arguments (#{data.length} for 1)"
        end
        super(self.class.instance_variable_get(:@name), data[0])
      end
    end

    alg.each do |name|
      klass = Class.new(Stub)
      klass.instance_variable_set(:@name, name)
      const_set(name, klass)
    end

    # This class is only provided for backwards compatibility.  Use OpenSSL::Digest in the future.
    class Digest < Digest
      def initialize(*args)
        # add warning
        super(*args)
      end
    end

  end # Digest
end # OpenSSL

