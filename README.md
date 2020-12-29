# SW multimedia streaming server

light rtp/rtsp server

1. this streaming server module has tested with VLC media player and live555 example code.
2. test.c has NOT tested enough. 

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
  
  
