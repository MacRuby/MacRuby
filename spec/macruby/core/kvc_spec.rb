require File.dirname(__FILE__) + "/../spec_helper"

class ScalarWrapper
  def kvcScalar
    @wrapped
  end

  def setKvcScalar(v)
    @wrapped = v
  end
end

class ArrayWrapper
  def initialize
    @wrapped = NSMutableArray.alloc.init
  end
end

class WholeArrayWrapper < ArrayWrapper
  def kvcOrderedCollection
    @wrapped
  end

  def setKvcOrderedCollection(newValue)
    @wrapped = newValue.mutableCopy
  end
end

class IndexedArrayWrapper < ArrayWrapper
  def countOfKvcOrderedCollection
    @wrapped.size
  end

  def objectInKvcOrderedCollectionAtIndex(idx)
    @wrapped[idx]
  end

  def insertObject(o, inKvcOrderedCollectionAtIndex:idx)
    @wrapped.insertObject(o, atIndex:idx)
  end

  def removeObjectFromKvcOrderedCollectionAtIndex(idx)
    @wrapped.removeObjectAtIndex(idx)
  end
end

class RIndexedArrayWrapper < IndexedArrayWrapper
  def replaceObjectInKvcOrderedCollectionAtIndex(idx, withObject:o)
    @wrapped[idx] = o
  end
end

class IndexSetArrayWrapper < ArrayWrapper
  def countOfKvcOrderedCollection
    @wrapped.count
  end

  def kvcOrderedCollectionAtIndexes(idx)
    @wrapped.objectsAtIndexes(idx)
  end

  def insertKvcOrderedCollection(o, atIndexes:idx)
    @wrapped.insertObjects(o, atIndexes:idx)
  end

  def removeKvcOrderedCollectionAtIndexes(idx)
    @wrapped.removeObjectsAtIndexes(idx)
  end
end

class RIndexSetArrayWrapper < IndexSetArrayWrapper
  def replaceKvcOrderedCollectionAtIndexes(idx, withKvcOrderedCollection:o)
    @wrapped.replaceObjectsAtIndexes(idx, withObjects:o)
  end
end

def manipulateOrderedCollection(w)
  1.upto(2) {|n| w.mutableArrayValueForKey("kvcOrderedCollection").addObject(n)}
  w.mutableArrayValueForKey("kvcOrderedCollection").addObjectsFromArray([7,8])
  4.downto(3) {|n| w.mutableArrayValueForKey("kvcOrderedCollection").insertObject(n, atIndex:2)}
  w.mutableArrayValueForKey("kvcOrderedCollection").insertObjects([5,6],
      atIndexes:NSIndexSet.indexSetWithIndexesInRange(NSRange.new(4, 2)))
  w.valueForKey("kvcOrderedCollection").should == (1..8).to_a

  w.mutableArrayValueForKey("kvcOrderedCollection").replaceObjectAtIndex(1, withObject:20)
  w.mutableArrayValueForKey("kvcOrderedCollection").replaceObjectsAtIndexes(
      NSIndexSet.indexSetWithIndex(3), withObjects:[40])
  w.mutableArrayValueForKey("kvcOrderedCollection").replaceObjectsInRange(
      NSRange.new(5,2), withObjectsFromArray:[60,70])
  w.valueForKey("kvcOrderedCollection").should == [1, 20, 3, 40, 5, 60, 70, 8]

  w.mutableArrayValueForKey("kvcOrderedCollection").removeObjectAtIndex(1)
  w.mutableArrayValueForKey("kvcOrderedCollection").removeObject(40)
  w.mutableArrayValueForKey("kvcOrderedCollection").removeObjectsAtIndexes(
      NSIndexSet.indexSetWithIndex(3))
  w.mutableArrayValueForKey("kvcOrderedCollection").removeObjectsInRange(
      NSRange.new(3, 1))
  w.valueForKey("kvcOrderedCollection").should == [1, 3, 5, 8]

  w.mutableArrayValueForKey("kvcOrderedCollection").removeAllObjects
  w.valueForKey("kvcOrderedCollection").should == []
end

class SetWrapper
  def initialize
    @wrapped = NSMutableSet.alloc.init
  end
end

class WholeSetWrapper < SetWrapper
  def kvcUnorderedCollection
    @wrapped
  end

  def setKvcUnorderedCollection(newValue)
    @wrapped = newValue.mutableCopy
  end
end

class AccessSetWrapper < SetWrapper
  def countOfKvcUnorderedCollection
    @wrapped.count
  end

  def enumeratorOfKvcUnorderedCollection
    @wrapped.objectEnumerator
  end

  def memberOfKvcUnorderedCollection(o)
    @wrapped.member(o)
  end
end

class ObjectSetWrapper < AccessSetWrapper
  def addKvcUnorderedCollectionObject(o)
    @wrapped.addObject(o)
  end

  def removeKvcUnorderedCollectionObject(o)
    @wrapped.removeObject(o)
  end    
end

class SetSetWrapper < AccessSetWrapper
  def addKvcUnorderedCollection(c)
    @wrapped.unionSet(c)
  end

  def removeKvcUnorderedCollection(c)
    @wrapped.minusSet(c)
  end    

  def intersectKvcUnorderedCollection(c)
    @wrapped.intersectSet(c)
  end    
end

def manipulateUnorderedCollection(w)
  3.upto(4) {|n| w.mutableSetValueForKey("kvcUnorderedCollection").addObject(n)}
  w.mutableSetValueForKey("kvcUnorderedCollection").addObjectsFromArray([1,2])
  w.mutableSetValueForKey("kvcUnorderedCollection").unionSet(NSSet.setWithArray([5,6]))
  w.valueForKey("kvcUnorderedCollection").isEqualToSet(NSSet.setWithArray((1..6).to_a)).should == true

  w.mutableSetValueForKey("kvcUnorderedCollection").removeObject(1)
  w.mutableSetValueForKey("kvcUnorderedCollection").intersectSet(NSSet.setWithArray([1,3,4,5,7]))
  w.mutableSetValueForKey("kvcUnorderedCollection").minusSet(NSSet.setWithArray([5,7]))
  w.valueForKey("kvcUnorderedCollection").isEqualToSet(NSSet.setWithArray([3,4])).should == true

  w.mutableSetValueForKey("kvcUnorderedCollection").removeAllObjects
  w.valueForKey("kvcUnorderedCollection").isEqualToSet(NSSet.set).should == true
end

describe "A scalar being accessed through NSKeyValueCoding" do
  it "can be manipulated" do
    w = ScalarWrapper.new
    w.setValue(7, forKey:"kvcScalar")
    w.valueForKey("kvcScalar").should == 7
  end
end

describe "An ordered collection being accessed through NSKeyValueCoding" do
  it "can be manipulated through whole-array accessors" do
    w = WholeArrayWrapper.new
    manipulateOrderedCollection(w)
  end

  it "can be manipulated through index accessors" do
    w = IndexedArrayWrapper.new
    manipulateOrderedCollection(w)
  end

  it "can be manipulated through index accessors (w/ replace)" do
    w = RIndexedArrayWrapper.new
    manipulateOrderedCollection(w)
  end

  it "can be manipulated through index set accessors" do
    w = IndexSetArrayWrapper.new
    manipulateOrderedCollection(w)
  end

  it "can be manipulated through index set accessors (w/ replace)" do
    w = RIndexSetArrayWrapper.new
    manipulateOrderedCollection(w)
  end
end

describe "An unordered collection being accessed through NSKeyValueCoding" do
  it "can be manipulated through whole-array accessors" do
    w = WholeSetWrapper.new
    manipulateUnorderedCollection(w)
  end

  it "can be manipulated through object accessors" do
    w = ObjectSetWrapper.new
    manipulateUnorderedCollection(w)
  end

  it "can be manipulated through set accessors" do
    w = SetSetWrapper.new
    manipulateUnorderedCollection(w)
  end
end

class Wrapper
  attr_accessor :whatever
end

describe "Module::attr_writer" do
  it "defines an Objective-C style setter method" do
    w = Wrapper.new
    w.setWhatever(42)
    w.whatever.should == 42
  end
end

class SubWrapper < Wrapper
  def whatever; 'whatever_value'; end
end

describe "A class inheriting from a class that defines a KVC attribute" do
  it "can also re-define it" do
    o = SubWrapper.new
    o.valueForKey('whatever').should == 'whatever_value'
    o.whatever = 'OMG'
    o.valueForKey('whatever').should == 'whatever_value'
    o.whatever.should == 'whatever_value'
  end
end
