class AppController
  attr_accessor :contestants
  attr_writer :winner

  def getWinner(sender)
    contestants = @contestants.stringValue.split(",")
	  winner = contestants[rand(contestants.length)]

	  @winner.stringValue = winner.strip
  end
end