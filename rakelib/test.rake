namespace :test do

  desc 'Run all unit tests from MRI'
  task :mri do
    sh "cd test-mri/bootstraptest && ./runner.rb"
  end

end
