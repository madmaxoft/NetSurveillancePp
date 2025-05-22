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

	/** Destroys the instance.
	First disconnects the main link. */
	~Recorder();

	/** Starts connecting and logging into the specified hostname + port.
	Returns immediately, before the actual connection is made.
	Reports the success or failure asynchronously using the provided callback function. */
	void connectAndLogin(
		const std::string & aHostName,
		uint16_t aPort,
		const std::string & aUserName,
		const std::string & aPassword,
		std::function<void(const std::error_code &)> aOnFinish
	);

	/** Disconnects from the device.
	Closes the connection and removes any ASIO work related to this device.
	Ignores any errors, returns immediately. */
	void disconnect();

	/** Asynchronously queries the channel names.
	Most devices require logging in first, before enumerating the channels (use connectAndLogin()).
	If successful, calls the callback with the channel names.
	On error, calls the callback with an error code and empty channel names. */
	void getChannelNames(Connection::ChannelNamesCallback aOnFinish);

	/** Asynchronously queries the specified SysInfo from the device.
	If successful, calls the callback with the SysInfo name and data.
	On error, calls the callback with an error code and the response received from the device. */
	void getSysInfo(Connection::SysInfoCallback aOnFinish, const std::string & aInfoName);

	/** Asynchronously queries the specified device config.
	If successful, calls the callback with the config name and data.
	On error, calls the callback with an error code and the response returned from the device. */
	void getConfig(Connection::ConfigCallback aOnFinish, const std::string & aConfigName);

	/** Installs an async alarm monitor.
	The callback is called whenever the device reports an alarm start or stop event.
	Only one monitor can be installed at a time, setting another one overwrites the previous one. */
	void monitorAlarms(Connection::AlarmCallback aOnAlarm);

	/** Asynchronously captures a picture from the specified channel. */
	void capturePicture(int aChannel, Connection::PictureCallback aOnFinish);


private:

	/** The main TCP connection to the device. */
	std::shared_ptr<Connection> mMainConnection;


	Recorder();
};

}  // namespace NetSurveillancePp
