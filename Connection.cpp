#include "Connection.hpp"

#include <format>

#include "Root.hpp"
#include "SofiaHash.hpp"
#include "Error.hpp"





namespace NetSurveillancePp
{





using LockGuard = std::lock_guard<std::recursive_mutex>;





////////////////////////////////////////////////////////////////////////////////
// Globals:

/** Adds 2 bytes into the output, containing the specified 16-bit value (Little-endian). */
static void writeUint16(std::vector<char> & aOutput, uint16_t aValue)
{
	aOutput.push_back(static_cast<char>(aValue        & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 8) & 0xff));
}





/** Adds 4 bytes into the output, containing the specified 32-bit value (Little-endian). */
static void writeUint32(std::vector<char> & aOutput, uint32_t aValue)
{
	aOutput.push_back(static_cast<char>(aValue         & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 8)  & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 16) & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 24) & 0xff));
}





/** Parses 2 bytes from the input as a 16-bit value. */
static uint16_t parseUint16(const char * aInput)
{
	auto input = reinterpret_cast<const uint8_t *>(aInput);
	return input[0] | (input[1] << 8);
}





/** Parses 4 bytes from the input as a 32-bit value. */
static uint32_t parseUint32(const char * aInput)
{
	auto input = reinterpret_cast<const uint8_t *>(aInput);
	return input[0] | (input[1] << 8) | (input[2] << 16) | (input[3] << 24);
}





////////////////////////////////////////////////////////////////////////////////
// Connection:

Connection::Connection():
	mSessionID(0),
	mSequence(0),
	mAliveInterval(0),
	mKeepAliveTimer(Root::instance().ioContext())
{
}





std::shared_ptr<Connection> Connection::create()
{
	return std::shared_ptr<Connection>(new Connection);
}





void Connection::connect(
	const std::string & aHostName,
	uint16_t aPort,
	std::function<void(const std::error_code &)> aOnFinish
)
{
	Super::connect(aHostName, aPort, aOnFinish);
}





void Connection::login(
	const std::string & aUsername,
	const std::string & aPassword,
	Callback aOnFinish
)
{
	std::string payload;
	nlohmann::json js =
	{
		{"LoginType", "DVRIP-Web"},
		{"EncryptType", "MD5"},
		{"UserName", aUsername},
		{"PassWord", sofiaHash(aPassword)},
	};
	queueCommand(CommandType::Login_Req, CommandType::Login_Resp, js.dump(),
		[self = selfPtr(), aOnFinish](const std::error_code & aError, const nlohmann::json & aResponse)
		{
			self->onLogin(aError, aResponse, aOnFinish);
		}
	);
}





void Connection::onLogin(const std::error_code & aError, const nlohmann::json & aResponse, Callback aOnFinish)
{
	if (aError)
	{
		return aOnFinish(aError, aResponse);
	}

	// Login was successful
	// Set the session ID from the response:
	auto itr = aResponse.find("SessionID");
	if (itr == aResponse.end())
	{
		return aOnFinish(Error::ResponseMissingExpectedField, aResponse);
	}
	mSessionID = std::stoi(std::string(*itr), nullptr, 0);

	// Schedule a keepalive packet according to the response:
	itr = aResponse.find("AliveInterval");
	if (itr == aResponse.end())
	{
		return aOnFinish(Error::ResponseMissingExpectedField, aResponse);
	}
	mAliveInterval = *itr;
	if (mAliveInterval > 0)
	{
		mKeepAliveTimer.expires_from_now(std::chrono::seconds(mAliveInterval / 2));
		mKeepAliveTimer.async_wait(
			[self = selfPtr()](const std::error_code & aError)
			{
				self->onKeepAliveTimer(aError);
			}
		);
	}

	// Report the success to the callback:
	return aOnFinish(aError, aResponse);
}





void Connection::onKeepAliveTimer(const std::error_code & aError)
{
	if (aError)
	{
		// Silently ignore all scheduling errors
		return;
	}
	nlohmann::json js =
	{
		{"Name", "KeepAlive"},
		{"SessionID", std::format("{:#08x}", mSessionID.load())},
	};
	queueCommand(CommandType::KeepAlive_Req, CommandType::KeepAlive_Resp, js.dump(),
		[](const std::error_code & aError, const nlohmann::json & aResponse)
		{
		}
	);
	mKeepAliveTimer.expires_from_now(std::chrono::seconds(mAliveInterval / 2));
	mKeepAliveTimer.async_wait(
		[self = selfPtr()](const std::error_code & aError)
		{
			self->onKeepAliveTimer(aError);
		}
	);
}





void Connection::queueCommand(
	CommandType aCommandType,
	CommandType aExpectedResponseType,
	const std::string & aPayload,
	Callback aOnFinish
)
{
	// Put the expected response type and handler to the incoming queue:
	{
		LockGuard lg(mMtxTransfer);
		mIncomingQueue.emplace_back(aExpectedResponseType, aOnFinish);
	}

	// Send the command:
	auto rawCmd = serializeCommand(aCommandType, aPayload);
	send(rawCmd);
}





std::vector<char> Connection::serializeCommand(CommandType aCommandType, const std::string & aPayload)
{
	auto seq = mSequence.fetch_add(1);
	std::vector<char> res;
	res.reserve(Protocol::HeaderLength + aPayload.size());
	res.push_back(Protocol::IDENTIFICATION);
	res.push_back(Protocol::VERSION);
	res.push_back(Protocol::RESERVED1);
	res.push_back(Protocol::RESERVED2);
	writeUint32(res, mSessionID);
	writeUint32(res, seq);
	res.push_back(Protocol::TOTALPKT);
	res.push_back(Protocol::CURRPKT);
	writeUint16(res, static_cast<uint16_t>(aCommandType));
	writeUint32(res, static_cast<uint32_t>(aPayload.size()));
	std::copy(aPayload.begin(), aPayload.end(), std::back_inserter(res));
	return res;
}






void Connection::parseIncomingPackets()
{
	size_t start = 0;
	while (mIncomingDataSize - start >= 20)
	{
		// Check if an entire packet is in the queue:
		if (mIncomingData[start] != Protocol::IDENTIFICATION)
		{
			return disconnected();
		}
		auto payloadLength = parseUint32(mIncomingData.data() + start + 16);
		if (mIncomingDataSize < payloadLength + Protocol::HeaderLength)
		{
			break;
		}

		// Parse the header and the json payload:
		auto messageType = parseUint16(mIncomingData.data() + start + 14);
		auto j = nlohmann::json::parse(mIncomingData.data() + start + 20, mIncomingData.data() + start + 20 + payloadLength, nullptr, false);
		if (j.is_discarded())
		{
			return disconnected();
		}
		auto itr = j.find("SessionID");
		if ((itr != j.end()) && (itr->is_number()))
		{
			mSessionID = itr->get<uint32_t>();
		}

		// Send the packet to the corresponding callback that is waiting in the queue:
		Callback callback = nullptr;
		{
			LockGuard lg(mMtxTransfer);
			for (auto itr = mIncomingQueue.begin(), end = mIncomingQueue.end(); itr != end; ++itr)
			{
				if (static_cast<uint16_t>(itr->first) == messageType)
				{
					callback = itr->second;
					mIncomingQueue.erase(itr);
					break;
				}
			}
		}
		if (callback != nullptr)
		{
			callback({}, j);
		}

		// Continue parsing:
		start += payloadLength + Protocol::HeaderLength;
	}

	// Remove the processed packets from the buffer:
	if ((start > 0) && (mIncomingDataSize > start))
	{
		std::memmove(mIncomingData.data(), mIncomingData.data() + start, mIncomingDataSize - start);
	}
	mIncomingDataSize -= start;
}





void Connection::disconnected()
{
	// Get a current copy of the incoming queue:
	decltype(mIncomingQueue) incomingQueue;
	{
		LockGuard lg(mMtxTransfer);
		std::swap(incomingQueue, mIncomingQueue);
	}

	// Notify all of the handlers that there was a disconnect:
	nlohmann::json empty;
	for (const auto & item: incomingQueue)
	{
		std::get<1>(item)(asio::error::eof, empty);
	}
}

}  // namespace NetSurveillancePp
