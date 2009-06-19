require 'hotcocoa'
include HotCocoa

TEXT_FILE = "http://www.gutenberg.org/files/4300/4300.txt"

class Application

  def start
    @app = application(:name   => "Download And Progress Bar") do |app|
      app.delegate = self
      @window = window(:frame     => [100, 100, 500, 500], :title => "Download And Progress Bar") do |win|
        win <<  label(:text => "Cocoa made easy! Example by Matt Aimonetti", :layout => {:start => false})
        @status = label(:text => "downloading remote data", :layout => {:start => false}, :frame => [0, 0, 300, 20])
        win <<  @status
        win << @progress_bar = progress_indicator
        
        # Setup a scroll view containing a text view which will display the downloaded data
        @scroll_view = scroll_view(:frame => [0,0,495,300], :layout => {:expand => [:height, :width]})
        @text_view = text_view(:frame => [0,0,490,300])
        @scroll_view.documentView = @text_view
        
        win << @scroll_view 
        
        @reload_button = button(:title => "Reload the data", :on_action => reload_data)
        @reload_button.hidden = true
        
        win << @reload_button
        
        initiate_request(TEXT_FILE, self)  
        win.will_close { exit }
      end
    end
  end
  
  # Returns a proc that is being called by the reload button
  def reload_data
    Proc.new { 
      # Change the progress bar_style
      @progress_bar.style = :spinning
      initiate_request(TEXT_FILE, self)
      @reload_button.hidden = true 
      @text_view.string = " Reloading..."
      @status.text  = 're downloading the text file'
    }
  end
  
  # Request helper setting up a connection and a delegate
  # used to monitor the transfer
  def initiate_request(url_string, delegator)
    url         = NSURL.URLWithString(url_string)
    request     = NSURLRequest.requestWithURL(url)
    @connection = NSURLConnection.connectionWithRequest(request, delegate:delegator)
  end
  
  # Delegate method
  #
  # The transfer is done and everything went well
  def connectionDidFinishLoading(connection)
    @status.text  = "Data totally retrieved"
    @progress_bar.hide
    @progress_bar.reset
    page          = NSString.alloc.initWithData(@receivedData, encoding:NSUTF8StringEncoding)
    @receivedData = nil
    begin 
      @text_view.string = page
    rescue
      @status.text = "couldn't display the loaded text"
    end
    NSLog("data fully received")
    @reload_button.hidden = false
  end
  
  # Delegate method
  #
  # Deal with the request response 
  # Note: in Ruby, the method name is the method signature
  # Objective C uses what's called a selector
  # The objc selector for the method below would be:
  #   `connection:didReceiveResponse:`
  def connection(connection, didReceiveResponse:response)
    @status.text = (response.statusCode == 200) ? "Retrieving data" : "There was an issue while trying to access the data"
    expected_size = response.expectedContentLength.to_f
    # If we know the size of the document we are downloading
    # we can determine the progress made so far
    if expected_size > 0
      @progress_bar.indeterminate = false
      @progress_bar.maxValue = expected_size.to_f
    end
    @progress_bar.show
    @progress_bar.start
    NSLog("extected response length: #{response.expectedContentLength.to_s}")
  end
  
  # Delegate method
  #
  # This delegate method get called every time a chunk of data
  # is being received
  def connection(connection, didReceiveData:receivedData)
    @status.text  = "Data being retrieved"
    # Initiate an ivar to store the received data
    @receivedData ||= NSMutableData.new
    @receivedData.appendData(receivedData)
    @progress_bar.incrementBy(receivedData.length.to_f)
    NSLog("data chunck received so far: #{@progress_bar.value.to_i}")
  end
  
end

Application.new.start