#!/usr/bin/env macruby

require File.expand_path('../../test_helper', __FILE__)

class TestHash < Test::Unit::TestCase

  def test_hash_class
    assert(Hash.is_a?(Class))
    assert(Hash.ancestors.include?(NSDictionary))
    assert(Hash.ancestors.include?(NSMutableDictionary))
  end

  def test_hash_create
    h = {}
    assert_kind_of(Hash, h)
    h = {'a' => 100}
    assert_kind_of(Hash, h)
    h = Hash.new
    assert_kind_of(Hash, h)
    h = Hash.alloc.init
    assert_kind_of(Hash, h)
    h = Hash['a', 100, 'b', 200]
    assert_kind_of(Hash, h)
    h = Hash['a' => 100, 'b' => 200]
    assert_kind_of(Hash, h)
  end

  class MyHash < Hash; end
  def test_hash_create_subclass
    assert(MyHash.ancestors.include?(Hash))
    h = MyHash.new
    assert_kind_of(MyHash, h)
    h = MyHash.alloc.init
    assert_kind_of(MyHash, h)
    h = MyHash['a', 100, 'b', 200]
    assert_kind_of(MyHash, h)
    h = MyHash['a' => 100, 'b' => 200]
    assert_kind_of(MyHash, h)
  end

  def test_hash_cannot_modify_immutable
    h = NSDictionary.new
    assert_raise(RuntimeError) { h[1] = 1 }
    assert_raise(RuntimeError) { h.clear }
    h = NSDictionary.alloc.init
    assert_raise(RuntimeError) { h[1] = 1 }
    assert_raise(RuntimeError) { h.clear }
  end

  def test_hash_get_set
    h = {}
    h[1] = 2
    assert_equal(2, h[1])
    h['foo'] = 3
    assert_equal(3, h['foo'])
    assert_equal(3, h[String.new('foo')])
    h[[1,2]] = 4
    assert_equal(4, h[[1,2]])
    assert_equal(4, h[Array.new([1,2])])
    h[:sym] = 5
    assert_equal(5, h[:sym])
    assert_equal(5, h['sym'.intern])
  end

  def test_hash_count
    h = {}
    assert_equal(0, h.size)
    assert_equal(0, h.count)
    assert(h.empty?)
    h[1] = 2
    assert_equal(1, h.size)
    assert_equal(1, h.count)
    assert(!h.empty?)
    h[1] = nil
    assert_equal(1, h.size)
    assert_equal(1, h.count)
    assert(!h.empty?)
    h.delete(1)
    assert_equal(0, h.size)
    assert_equal(0, h.count)
    assert(h.empty?)
  end

  def test_hash_clear
    h = {1=>2}
    h.clear
    assert(h.empty?)
    assert_equal(nil, h[1])
  end

end
