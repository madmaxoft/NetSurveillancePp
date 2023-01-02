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
	using Callback = std::function<void(const asio::error_code &, const nlohmann::json &)>;

	enum class CommandType: uint16_t
	{
		Login_Req = 1000,
		Login_Resp = 1000,
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
		std::function<void(const asio::error_code &)> aOnFinish
	);

	/** Asynchronously logs in using the specified credentials.
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


	/** Protected constructor, we want the clients to use std::shared_ptr for owning this object.
	Instead of a constructor, the clients should call create(). */
	Connection();

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
