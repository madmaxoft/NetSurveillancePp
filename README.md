# NetSurveillancePp
(WIP) C++ library for talking the NETSurveillance (Sofia) protocol used by the cheap DVRs / NVRs.


## Documentation

The library uses Asio for the networking and asynchronicity. The library manages all of its asio-processing background threads opaquely.

Classes provided by the library:
| Class           | Notes |
| :-------------- | :--- |
| NetSurveillance | The parent object for all classes provided by this class. Internally, it houses the asio's `io_context` used for communicating and the background threads on which it runs. |
| Recorder        | A single NVR or DVR unit to which a network connection can be made. |
| Camera          | A single camera (within the Recorder) that can provide a video stream. |


## Building

The library uses CMake for building. It expects that it is used from another application as a static-link library, and that the top level app's CMakeLists.txt will make sure that Asio is available as an `asio::standalone` target. Look at https://github.com/madmaxoft/NetSurveillancePp-Tests for an example of a complete setup.
