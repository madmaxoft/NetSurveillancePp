#include "Connection.hpp"

#include <fmt/format.h>

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
	JsonCallback aOnFinish
)
{
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
			self->onLoginResp(aError, aResponse, aOnFinish);
		}
	);
}





void Connection::getChannelNames(ChannelNamesCallback aOnFinish)
{
	nlohmann::json js =
	{
		{"SessionID", sessionIDHexStr()},
		{"Name",      "ChannelTitle"},
	};
	queueCommand(CommandType::ConfigChannelTitleGet_Req, CommandType::ConfigChannelTitleGet_Resp, js.dump(),
		[self = selfPtr(), aOnFinish](const std::error_code & aError, const nlohmann::json & aResponse)
		{
			self->onGetChannelNamesResp(aError, aResponse, aOnFinish);
		}
	);
}





void Connection::onLoginResp(const std::error_code & aError, const nlohmann::json & aResponse, JsonCallback aOnFinish)
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





void Connection::onGetChannelNamesResp(const std::error_code & aError, const nlohmann::json & aResponse, ChannelNamesCallback aOnFinish)
{
	if (aError)
	{
		return aOnFinish(aError, {});
	}

	// Parse the channel names from the Json:
	auto itr = aResponse.find("ChannelTitle");
	if (itr == aResponse.end())
	{
		return aOnFinish(make_error_code(Error::ResponseMissingExpectedField), {});
	}
	std::vector<std::string> channelTitles;
	for (const auto & cht: *itr)
	{
		channelTitles.push_back(cht);
	}
	return aOnFinish({}, channelTitles);
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
		{"SessionID", sessionIDHexStr()},
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




std::string Connection::sessionIDHexStr() const
{
	return fmt::format("{:#08x}", mSessionID.load());
}





void Connection::queueCommand(
	CommandType aCommandType,
	CommandType aExpectedResponseType,
	const std::string & aPayload,
	JsonCallback aOnFinish
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

		// Find the corresponding callback that is waiting in the queue:
		JsonCallback callback = nullptr;
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

		// Parse the return code from the packet, call the callback:
		if (callback != nullptr)
		{
			itr = j.find("Ret");
			if ((itr == j.end()) || (!itr->is_number()))
			{
				callback(make_error_code(Error::ResponseMissingExpectedField), j);
			}
			else
			{
				if (*itr == Error::Success)
				{
					callback({}, j);
				}
				else
				{
					callback(make_error_code(static_cast<Error>(*itr)), j);
				}
			}
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
