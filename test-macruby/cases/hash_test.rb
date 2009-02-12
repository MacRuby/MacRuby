#!/usr/bin/env macruby

require File.expand_path('../../test_helper', __FILE__)

class MacRuby
  class HashTest < Test::Unit::TestCase
    class HashSubclass < Hash; end

    it "should initialize as an empty hash with the `{}' shortcut" do
      assert_kind_of Hash, {}
    end

    # FIXME: This causes a segfault.
    it "should not modify an immutable instance and raise a RuntimeError" do
      [NSDictionary.new, NSDictionary.alloc.init].each do |hash|
        assert_raise(RuntimeError) { hash.clear }
        assert_raise(RuntimeError) { hash[:key] = :value }
        assert_nil hash[:key]
      end
    end

    [Hash, HashSubclass].each do |klass|
      it "should be a subclass of NSMutableDictionary `#{klass.name}'" do
        assert klass.is_a? Class
        assert klass.ancestors.include? NSDictionary
        assert klass.ancestors.include? NSMutableDictionary
      end

      it "should initialize as an empty hash `#{klass.name}'" do
        [klass.new, klass.alloc.init].each do |hash|
          assert_kind_of klass, hash
          assert_equal({}, hash)
        end
      end

      # FIXME: This returns a NSMutableDictionary instead of a HashSubclass.
      it "should initialize with key-value pairs `#{klass.name}'" do
        hashes = [
          { :is_set? => true },
          klass[:is_set?, true],
          klass[:is_set? => true]
        ]

        hashes.each do |hash|
          assert_kind_of klass, hash
          assert_equal true, hash[:is_set?]
        end
      end

      it "should assign key-value pairs `#{klass.name}'" do
        hash = klass.new
        keys = [1, 'key', :key, %w{ array key }]
        keys.each { |key| hash[key] = :value }

        keys.each { |key| assert_equal :value, hash[key] }
      end

      it "should return the number of key-value pairs in the collection `#{klass.name}'" do
        hash = klass.new
        assert_empty_and_and_length_of_0(hash)

        hash[:key] = :value
        assert_not_empty_and_length_of_1(hash)

        hash.delete(:key)
        assert_empty_and_and_length_of_0(hash)
      end

      it "should clear its contents `#{klass.name}'" do
        hash = klass.new
        hash[:key] = :value
        assert !hash.empty?

        hash.clear
        assert hash.empty?
        assert !hash.has_key?(:key)
      end

      # FIXME: Fails for subclasses.
      it "should return a duplicate _with_ default proc `#{klass.name}'" do
        hash = klass.new { |_,k| k }.dup
        assert_equal :default, hash[:default]
      end
    end

    private

    def assert_empty_and_and_length_of_0(hash)
      assert hash.empty?
      assert_equal 0, hash.size
      assert_equal 0, hash.count
      assert_equal 0, hash.length
    end

    def assert_not_empty_and_length_of_1(hash)
      assert !hash.empty?
      assert_equal 1, hash.size
      assert_equal 1, hash.count
      assert_equal 1, hash.length
    end
  end
end