#pragma once

#include "TcpConnection.hpp"
#include <nlohmann/json.hpp>





namespace NetSurveillancePp
{




/** Constants used in the protocol. */
namespace Protocol
{
	constexpr uint32_t HeaderLength = 20;  // Number of bytes in the header
	constexpr char IDENTIFICATION = static_cast<char>(0xff);
	constexpr char VERSION        = 0x00;  // Doc says 0x01, device sends 0x01, VMS and CMS send 0x00. Probably not important.
	constexpr char RESERVED1      = 0x00;
	constexpr char RESERVED2      = 0x00;
	constexpr char TOTALPKT       = 0x00;  // Never saw this packetization at work
	constexpr char CURRPKT        = 0x00;
};





/** Represents a single TCP connection to the device.
Provides the protocol serializing and parsing. */
class Connection:
	public TcpConnection
{
	using Super = TcpConnection;

public:

	/** The callback for incoming data. */
	using Callback = std::function<void(const std::error_code &, const nlohmann::json &)>;

	enum class CommandType: uint16_t
	{
		// Note: The following values are off-by-one from the official docs, but are what was seen on wire on a real device
		Login_Req        = 1000,
		Login_Resp       = 1001,
		Logout_Req       = 1002,
		Logout_Resp      = 1003,
		ForceLogout_Req  = 1004,
		ForceLogout_Resp = 1005,
		KeepAlive_Req    = 1006,
		KeepAlive_Resp   = 1007,
		// (end of off-by-one values)
	};


	/** Creates a new instance of this class.
	Because of lifetime management, this class can only ever exist owned by a shared_ptr, therefore clients need to use
	create() instead of a constructor. */
	static std::shared_ptr<Connection> create();

	/** Asynchronously connects to the specified host + port. 
	Returns immediately, calls the finish handler async afterwards from an ASIO worker thread. */
	void connect(
		const std::string & aHostName,
		uint16_t aPort,
		std::function<void(const std::error_code &)> aOnFinish
	);

	/** Asynchronously logs in using the specified credentials.
	When successful, schedules sending a keepalive packet according to the device's requirements.
	Returns immediately, calls the finish handler async afterwards from an ASIO worker thread. */
	void login(
		const std::string & aUsername,
		const std::string & aPassword,
		Callback aOnFinish
	);


protected:

	/** The session ID, assigned by the device.
	Zero if not yet set. */
	std::atomic<uint32_t> mSessionID;

	/** The queue of expected response types and their completion handlers waiting for received data.
	Protected against multithreaded access by mMtxTransfer. */
	std::vector<std::pair<CommandType, Callback>> mIncomingQueue;

	/** The sequence counter for outgoing packets. */
	std::atomic<uint32_t> mSequence;

	/** The AliveInterval received from the device in the Login_Resp packet.
	Specifies the interval, in seconds, between the KeepAlive packets required by the device. */
	int mAliveInterval;

	/** The ASIO timer used for seinding KeepAlive requests. */
	asio::steady_timer mKeepAliveTimer;


	/** Protected constructor, we want the clients to use std::shared_ptr for owning this object.
	Instead of a constructor, the clients should call create(). */
	Connection();

	/** Returns a correctly typed shared_ptr to self.
	Equivalent to shared_from_this(), but typed correctly for this class. */
	std::shared_ptr<Connection> selfPtr() { return std::static_pointer_cast<Connection>(shared_from_this()); }

	/** Reads the login response, sets up the session ID and keepalives, calls the finish handler.
	If an error is reported, only farwards the error to the finish handler.
	Called by ASIO when a login response has been received. */
	void onLogin(const std::error_code & aError, const nlohmann::json & aResponse, Callback aOnFinish);

	/** Queues a KeepAlive request and re-schedules the timer again.
	Called by ASIO periodically (through mKeepAliveTimer). */
	void onKeepAliveTimer(const std::error_code & aError);

	/** Puts the specified command to the send queue to be sent async.
	Once the reply for the command is received, calls the callback from an ASIO worker thread. */
	void queueCommand(
		CommandType aCommandType,
		CommandType aExpectedResponseType,
		const std::string & aPayload,
		Callback aOnFinish
	);

	/** Serializes the specified command into the on-wire format. */
	std::vector<char> serializeCommand(CommandType aCommandType, const std::string & aPayload);

	/** Parses mIncomingData for any incoming packets, processes them and removes them from mIncomingData / mIncomingDataSize.
	Implements the functionality used by the underlying TcpConnection. */
	virtual void parseIncomingPackets() override;

	/** Notifies all waiting handlers that the socket has closed.
	Implements the functionality used by the underlying TcpConnection. */
	virtual void disconnected() override;
};





}  // namespace NetSurveillancePp
