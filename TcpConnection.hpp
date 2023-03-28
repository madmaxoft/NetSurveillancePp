#pragma once

#include <string>
#include <functional>
#include <asio.hpp>




namespace NetSurveillancePp
{




/** Represents a single generic TCP connection.
Provides a more usual facade for working with network connection than ASIO:
Handles the details of queueing the outgoing data, so that clients need only call TcpConnection::send(...)
Handles incoming data by queueing it ito a buffer and calling a virtual function parseIncomingPackets() so
that a descendant may process the data.
Usage:
Create a descendant from this class and call the send() function to send data,
override parseIncomingPackets() to receive data,
override disconnect() to react to disconnecting.
Note that this class must be wrapped in a std::shared_ptr<> because of lifetime constraints. */
class TcpConnection:
	public std::enable_shared_from_this<TcpConnection>
{
public:

	TcpConnection();

	/** Asynchronously connects to the specified host + port. 
	Returns immediately, calls the finish handler async afterwards from an ASIO worker thread. */
	void connect(
		const std::string & aHostName,
		uint16_t aPort,
		std::function<void(const std::error_code &)> aOnFinish
	);

	/** Asynchronously sends the specified data.
	Returns immediately, there is no notification about having sent the data. */
	void send(const std::vector<char> & aData);

	/** Disconnects the socket.
	Ignores any errors, returns immediately. */
	void disconnect();


protected:

	/** The resolver used for DNS lookup while connecting. */
	asio::ip::tcp::resolver mResolver;

	/** The TCP socket represented in this object. */
	asio::ip::tcp::socket mSocket;

	/** The data queued for sending over the mSocket (ASIO buffer).
	If an outgoing packet is in-flight (mIsOutgoing is true), the buffer is used for an outgoing operation and must not be touched. */
	std::vector<char> mOutgoingData;

	/** Flag whether there is an outgoing packet in-flight.
	If true, mOutgoingData contains the packet and must not be modified.
	If false, there's no outgoing packet, mOutgoingData may be freely modified.
	Protected against multithreaded access by mMtxTransfer. */
	bool mIsOutgoing;

	/** The queue for data to be transfered out.
	Protected against multithreaded access by mMtxTransfer. */
	std::vector<char> mOutgoingQueue;

	/** The data incoming from mSocket (ASIO buffer).
	mIncomingDataSize specifies how many bytes from this are valid. */
	std::array<char, 128 * 1024> mIncomingData;

	/** The number of bytes in mIncomingData that are valid. */
	std::size_t mIncomingDataSize;

	/** The mutex protecting mIsOutgoing, mOutgoingQueue, mIncomingQueue against multithreaded access. */
	std::recursive_mutex mMtxTransfer;

	/** Flag specifying whether the socket is connected. */
	bool mIsConnected;


	/** Queues another read operation with ASIO. */
	void queueRead();

	/** Called by ASIO when mOutgoingData has been written to mSocket. */
	void onWritten(const std::error_code & aError);

	/** Called by ASIO when data has been read into mIncomingData. */
	void onRead(const std::error_code & aError, std::size_t aNumBytes);

	/** Takes next item in mOutgoingQueue, if available, and starts writing it.
	Moves the item from mOutgoingQueue into mOutgoingData / mIncomingQueue.
	Assumes that mMtxTransfer is held by the caller. */
	void writeNextQueueItem();

	/** Parses mIncomingData for any incoming packets, processes them and removes them from mIncomingData / mIncomingDataSize.
	Descendants provide this protocol-specific functionality. */
	virtual void parseIncomingPackets() = 0;

	/** Called when a disconnect is detected on the socket.
	Descendants provide specific functionality for this. */
	virtual void disconnected() = 0;
};





}  // namespace NetSurveillancePp
