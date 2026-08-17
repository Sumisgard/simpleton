#!/usr/bin/fish

for port in 6767 6767 6767 6767
  nc localhost $port &
end
wait
