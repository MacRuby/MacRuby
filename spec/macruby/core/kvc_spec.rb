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
    @callLog = Hash.new(0)
  end

  def logCall(symbol)
    @callLog[symbol] += 1
  end

  def callLog
    @callLog
  end
end

class WholeArrayWrapper < ArrayWrapper
  def kvcOrderedCollection
    logCall(:kvcOrderedCollection)
    @wrapped
  end

  def setKvcOrderedCollection(newValue)
    logCall(:setKvcOrderedCollection)
    @wrapped = newValue.mutableCopy
  end
end

class IndexedArrayWrapper < ArrayWrapper
  def countOfKvcOrderedCollection
    logCall(:countOfKvcOrderedCollection)
    @wrapped.size
  end

  def objectInKvcOrderedCollectionAtIndex(idx)
    logCall(:objectInKvcOrderedCollectionAtIndex)
    @wrapped[idx]
  end

  def insertObject(o, inKvcOrderedCollectionAtIndex:idx)
    logCall(:insertObjectinKvcOrderedCollectionAtIndex)
    @wrapped.insertObject(o, atIndex:idx)
  end

  def removeObjectFromKvcOrderedCollectionAtIndex(idx)
    logCall(:removeObjectFromKvcOrderedCollectionAtIndex)
    @wrapped.removeObjectAtIndex(idx)
  end
end

class RIndexedArrayWrapper < IndexedArrayWrapper
  def replaceObjectInKvcOrderedCollectionAtIndex(idx, withObject:o)
    logCall(:replaceObjectInKvcOrderedCollectionAtIndexWithObject)
    @wrapped[idx] = o
  end
end

class IndexSetArrayWrapper < ArrayWrapper
  def countOfKvcOrderedCollection
    logCall(:countOfKvcOrderedCollection)
    @wrapped.size
  end

  def kvcOrderedCollectionAtIndexes(idx)
    logCall(:kvcOrderedCollectionAtIndexes)
    @wrapped.objectsAtIndexes(idx)
  end

  def insertKvcOrderedCollection(o, atIndexes:idx)
    logCall(:insertKvcOrderedCollectionAtIndexes)
    @wrapped.insertObjects(o, atIndexes:idx)
  end

  def removeKvcOrderedCollectionAtIndexes(idx)
    logCall(:removeKvcOrderedCollectionAtIndexes)
    @wrapped.removeObjectsAtIndexes(idx)
  end
end

class RIndexSetArrayWrapper < IndexSetArrayWrapper
  def replaceKvcOrderedCollectionAtIndexes(idx, withKvcOrderedCollection:o)
    logCall(:replaceKvcOrderedCollectionAtIndexesWithKvcOrderedCollection)
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
  w.mutableArrayValueForKey("kvcOrderedCollection").removeObjectIdenticalTo(40)
  w.mutableArrayValueForKey("kvcOrderedCollection").removeObjectsAtIndexes(
      NSIndexSet.indexSetWithIndex(3))
  w.mutableArrayValueForKey("kvcOrderedCollection").removeObjectsInRange(
      NSRange.new(3, 1))
  w.valueForKey("kvcOrderedCollection").should == [1, 3, 5, 8]

  w.mutableArrayValueForKey("kvcOrderedCollection").removeAllObjects
  w.valueForKey("kvcOrderedCollection").should == []
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
    w.callLog[:kvcOrderedCollection].should >= 16
    w.callLog[:setKvcOrderedCollection].should >= 12
  end

  it "can be manipulated through index accessors" do
    w = IndexedArrayWrapper.new
    manipulateOrderedCollection(w)
    w.callLog[:countOfKvcOrderedCollection].should > 0
    w.callLog[:objectInKvcOrderedCollectionAtIndex].should > 0
    w.callLog[:insertObjectinKvcOrderedCollectionAtIndex].should > 0
    w.callLog[:removeObjectFromKvcOrderedCollectionAtIndex].should > 0
  end

  it "can be manipulated through index accessors (w/ replace)" do
    w = RIndexedArrayWrapper.new
    manipulateOrderedCollection(w)
    w.callLog[:countOfKvcOrderedCollection].should > 0
    w.callLog[:objectInKvcOrderedCollectionAtIndex].should > 0
    w.callLog[:insertObjectinKvcOrderedCollectionAtIndex].should > 0
    w.callLog[:removeObjectFromKvcOrderedCollectionAtIndex].should > 0
    w.callLog[:replaceObjectInKvcOrderedCollectionAtIndexWithObject].should > 0
  end

  it "can be manipulated through index set accessors" do
    w = IndexSetArrayWrapper.new
    manipulateOrderedCollection(w)
    w.callLog[:countOfKvcOrderedCollection].should > 0
    w.callLog[:kvcOrderedCollectionAtIndexes].should > 0
    w.callLog[:insertKvcOrderedCollectionAtIndexes].should > 0
    w.callLog[:removeKvcOrderedCollectionAtIndexes].should > 0
  end

  it "can be manipulated through index set accessors (w/ replace)" do
    w = RIndexSetArrayWrapper.new
    manipulateOrderedCollection(w)
    w.callLog[:countOfKvcOrderedCollection].should > 0
    w.callLog[:kvcOrderedCollectionAtIndexes].should > 0
    w.callLog[:insertKvcOrderedCollectionAtIndexes].should > 0
    w.callLog[:removeKvcOrderedCollectionAtIndexes].should > 0
    w.callLog[:replaceKvcOrderedCollectionAtIndexesWithKvcOrderedCollection].should > 0
  end
end
