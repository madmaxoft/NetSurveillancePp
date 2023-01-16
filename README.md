# NetSurveillancePp
(WIP) C++ library for talking the NETSurveillance (Sofia) protocol used by the cheap DVRs / NVRs.


## Documentation

(Proper documentation still missing)

Classes provided by the library:
| Class           | Notes |
| :-------------- | :--- |
| Root            | The singleton used by other classes. Internally, it houses the asio's `io_context` used for communicating and the background threads on which it runs. |
| Recorder        | A single NVR or DVR unit to which a network connection can be made. |
| Camera          | A single camera (within the Recorder) that can provide a video stream. |

The library uses Asio for the networking and asynchronicity. The library manages all of its asio-processing background threads opaquely.


## Building

The library uses CMake for building. It expects that it is used from another application as a static-link library, and that the top level app's CMakeLists.txt will make sure that the following libraries are available:

- [Asio](https://github.com/madmaxoft/asio) as an `asio::standalone` target
- [Fmt](https://github.com/madmaxoft/fmt) as an `fmt::fmt-header-only` target
- [Nlohmann-json](https://github.com/madmaxoft/nlohmann-json) as `nlohmann_json::nlohmann_json` target

Look at https://github.com/madmaxoft/NetSurveillancePp-Tests for an example of a complete setup.
