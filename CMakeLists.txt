cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(NetSurveillancePp LANGUAGES CXX)





set(SRCS
	Connection.cpp
	Error.cpp
	Recorder.cpp
	Root.cpp
	SofiaHash.cpp
	TcpConnection.cpp
)

set (HDRS
	Connection.hpp
	Error.hpp
	Recorder.hpp
	Root.hpp
	SofiaHash.hpp
	TcpConnection.hpp
)





# Add the static library:
add_library(NetSurveillancePp-static STATIC ${SRCS} ${HDRS})
target_link_libraries(NetSurveillancePp-static asio::standalone nlohmann_json::nlohmann_json)
target_compile_features(NetSurveillancePp-static PRIVATE cxx_std_20)
message("Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(NetSurveillancePp-static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "blabla")
