describe :sha384_length, :shared => true do
  ruby_version_is "" ... "1.9" do
    it 'returns the length of the digest' do
      cur_digest = Digest::SHA384.new
      cur_digest.send(@method).should == SHA384Constants::BlankDigest.size
      cur_digest << SHA384Constants::Contents
      cur_digest.send(@method).should == SHA384Constants::Digest.size
    end
  end
  ruby_version_is "1.9" do
    it 'returns the length of the digest' do
      cur_digest = Digest::SHA384.new
      cur_digest.send(@method).should == SHA384Constants::BlankDigest.dup.force_encoding('ascii-8bit').size
      cur_digest << SHA384Constants::Contents
      cur_digest.send(@method).should == SHA384Constants::Digest.dup.force_encoding('ascii-8bit').size
    end
  end
end
