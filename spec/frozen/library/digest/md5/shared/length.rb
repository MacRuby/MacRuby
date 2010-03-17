describe :md5_length, :shared => true do
  ruby_version_is "" ... "1.9" do
    it 'returns the length of the digest' do
      cur_digest = Digest::MD5.new
      cur_digest.send(@method).should == MD5Constants::BlankDigest.size
      cur_digest << MD5Constants::Contents
      cur_digest.send(@method).should == MD5Constants::Digest.size
    end
  end

  ruby_version_is "1.9" do
    it 'returns the length of the digest' do
      cur_digest = Digest::MD5.new
      cur_digest.send(@method).should == MD5Constants::BlankDigest.dup.force_encoding('ascii-8bit').size
      cur_digest << MD5Constants::Contents
      cur_digest.send(@method).should == MD5Constants::Digest.dup.force_encoding('ascii-8bit').size
    end
  end
end
