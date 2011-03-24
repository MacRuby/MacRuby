$false_is_not_true = false.should.not.be.true
$nil_is_not_true = nil.should.not.be.true

describe 'A non-true value' do
  it 'should pass negated tests inside specs' do
    false.should.not.be.true
    nil.should.not.be.true
  end
  
  it 'should pass negated tests outside specs' do
    $false_is_not_true.should.be.true
    $nil_is_not_true.should.be.true
  end
end
