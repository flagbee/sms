# SW multimedia streaming server

light rtp/rtsp server

1. this streaming server module has been tested with VLC media player and live555 example code.
2. The module lib has been test enough but test.c has NOT tested yet.

## build

  mkdir build
  
  cd build
  
  cmake ../
  
  make -j

## Running test code

  cd build
  
  cd example
  
  ./testServer
  
  and it can be pulled by rtsp://host:5555/live/primary
  
  
