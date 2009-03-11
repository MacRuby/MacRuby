def distanceSquaredBetweenPoints(p1, p2)
  deltaX = p1.x - p2.x
  deltaY = p1.y - p2.y
  (deltaX * deltaX) - (deltaY * deltaY)
end

def distanceBetweenPoints(p1, p2)
  Math.sqrt distanceSquaredBetweenPoints(p1, p2)
end