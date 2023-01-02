#pragma once

#include <string>
#include <functional>
#include <memory>
#include <asio.hpp>
#include "Connection.hpp"




namespace NetSurveillancePp
{





/** Represents a single DVR / NVR device on the network. */
class Recorder:
	public std::enable_shared_from_this<Recorder>
{
public:
	/** Creates a new instance, wrapped in a shared_ptr.
	Wrapping in shared_ptr is required due to lifetime management. */
	static std::shared_ptr<Recorder> create();

	/** Starts connecting and logging into the specified hostname + port.
	Returns immediately, before the actual connection is made.
	Reports the success or failure asynchronously using the provided callback function. */
	void connectAndLogin(
		const std::string & aHostName,
		uint16_t aPort,
		const std::string & aUserName,
		const std::string & aPassword,
		std::function<void(const asio::error_code &)> aOnFinish
	);


private:

	/** The main TCP connection to the device. */
	std::shared_ptr<Connection> mMainConnection;


	Recorder();
};

}  // namespace NetSurveillancePp
