require 'rake/testtask'

NAME 		= 'Ruby-Sketch'
APP_VERSION = '1.0'
IDENTIFIER 	= "com.yourcompany.#{NAME}"

ARCH 		= '-arch x86_64'
FRAMEWORKS 	= '-framework MacRuby -framework Foundation -framework Cocoa'
GCFLAG 		= '-fobjc-gc-only'

CONTENTS_DIR 	= "#{NAME}.app/Contents"
RESOURCE_DIR 	= File.join(CONTENTS_DIR, 'Resources')
MACOS_DIR 		= File.join(CONTENTS_DIR, 'MacOS')
FRAMEWORKS_DIR	= File.join(CONTENTS_DIR, 'Frameworks')

task :default => :run

# Create a transformation task and associate with taskSymbol for a set of
# input files.	The transformation is passed as a block with target and src
# parameters. The input files can be a file list or a glob search pattern. The
# target directory given will be used and any intermediate directories to
# reach the target will be created.	The target's extension, if different, is
# given otherwise the same extension will be used.
def transformTask (taskSymbol, fl_or_srcGlob, targetDir, targetExt = nil, &block)
	fileList = fl_or_srcGlob.kind_of?(FileList) ? fl_or_srcGlob : FileList[fl_or_srcGlob]
	fileList.each do |src|
		target = File.join(targetDir, src)
		target = target.ext(targetExt) if targetExt
		file target => [src] do
			mkdir_p(target.pathmap("%d"), :verbose => false)
			block.call(target, src)
		end
		task taskSymbol => target
	end
end

# Empty tasks incase no files of this type are in the project.
task :xib

COPY_FILES = FileList["*.rb", "*.tiff", "*.icns", "*.strings", "*.sdef", "*.scriptSuite",
						"English.lproj/*.strings",
						"English.lproj/*.scriptTerminology"]

# Set up all the dependencies and tasks
transformTask(:copy_files, COPY_FILES, RESOURCE_DIR) {|target, src| cp_r(src, target)}
transformTask(:info_plist, "Info.plist", CONTENTS_DIR) {|target, src| sh "ruby -p -e 'sub(/\\$\\{EXECUTABLE_NAME\\}/, \"#{NAME}\")' <#{src} >#{target}"}
transformTask(:xib, "English.lproj/*.nib", RESOURCE_DIR) {|target, src| sh "ibtool --compile #{target} #{src}"}

task 'Build application'
task :build => [File.join(MACOS_DIR, NAME), :copy_files, :xib, :pkginfo, :info_plist]

file File.join(MACOS_DIR, NAME) => ["main.m", :copy_files] do |t|
	mkdir_p("#{MACOS_DIR}", :verbose => false)
	sh "gcc main.m -L#{FRAMEWORKS_DIR} -o #{t.name} #{ARCH} #{FRAMEWORKS} #{GCFLAG}"
end
	
desc 'Write the Pkginfo plist'
task :pkginfo do
	File.open("#{CONTENTS_DIR}/PkgInfo", "w") {|f| f.puts "APPLsktc"}
end

desc 'Run application'
task :run => [:build] do
	`#{MACOS_DIR}/#{NAME}`
end

desc 'Execute application bundle'
task :exec => [:build] do
	`open #{NAME}.app`
end

desc 'Clean'
task :clean do
	rm_rf("#{NAME}.app")
end

desc 'Build deploy compiled application'
task :deploy => [:clean, :build] do
	sh "macruby_deploy --compile --verbose #{NAME}.app"
end

desc 'Build deploy compiled application with embedded macruby'
task :deploy_embedded => [:clean, :build] do
	sh "macruby_deploy --embed --verbose #{NAME}.app --no-stdlib"
end
