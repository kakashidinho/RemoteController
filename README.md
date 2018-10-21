This is a library allowing remote control an app through network.
Particularly useful when you want to use your PC to remote control a mobile app.

The mobile app's display will be streamed to PC, and mouse clicks on PC will be
treated as touch controls in mobile app for example.

I created this library to support [Multiness](https://lehoangquyenblog.wordpress.com/published-games/74-2/)
Online Multiplayer feature.

## Basic overview
* There are two endpoints: Client and Server.
* Server will capture rendered frame and send to client.
* Client can then receive the frame sent from Server by calling `getFrameEvent`.
* Note that how Server capture current frame depends on the component `IFrameCapturer` supplied when you construct Server object.
* See `FrameCapturer.h` and `FrameCapturerGL.h` for example of `IFrameCapturer` implementation that captures current OpenGL's display buffer.
* Before the captured frame is sent to Client. It needs to be compressed. Hence the component `IImgCompressor` needs to be supplied to Server's constructor also.
* Finally, `IAudioCapturer` can be supplied if you want Server to stream audio data also. It is optional.
* There is no example code currently, if you want, you can take a look at (Multiness source code)[https://github.com/kakashidinho/Multiness]. Specifically, its `source/core/NstMachine.cpp`, where both Client and Server are used. The code there is quite hackish, though. Since that project originally was my experiment for playing online with my oversea friends. And I'm too lazy to clean them up.

## License
This library is licensed under Apache License, Version 2.0. See the file `NOTICE`
for more details.
