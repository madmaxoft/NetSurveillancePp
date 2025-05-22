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





void Connection::getSysInfo(SysInfoCallback aOnFinish, const std::string & aInfoName)
{
	nlohmann::json js =
	{
		{"SessionID", sessionIDHexStr()},
		{"Name",      aInfoName},
	};
	queueCommand(CommandType::SysInfo_Req, CommandType::SysInfo_Resp, js.dump(),
		[aOnFinish, aInfoName](const std::error_code & aError, const nlohmann::json & aResponse)
		{
			aOnFinish(aError, aInfoName, aResponse);
		}
	);
}





void Connection::getConfig(ConfigCallback aOnFinish, const std::string & aConfigName)
{
	nlohmann::json js =
	{
		{"SessionID", sessionIDHexStr()},
		{"Name",      aConfigName},
	};
	queueCommand(CommandType::ConfigGet_Req, CommandType::ConfigGet_Resp, js.dump(),
		[aOnFinish, aConfigName](const std::error_code & aError, const nlohmann::json & aResponse)
		{
			aOnFinish(aError, aConfigName, aResponse);
		}
	);
}





void Connection::monitorAlarms(Connection::AlarmCallback aOnAlarm)
{
	std::swap(aOnAlarm, mOnAlarm);
	if (aOnAlarm != nullptr)
	{
		// The device was already monitoring for alarms, no need to ask it to start
		return;
	}

	// The alarms were not monitored from the device before, start monitoring:
	nlohmann::json js =
	{
		{"Name", ""},
		{"SessionID", sessionIDHexStr()},
	};
	queueCommand(CommandType::Guard_Req, CommandType::Guard_Resp, js.dump(),
		[aOnAlarm](const std::error_code & aError, const nlohmann::json & aResponse)
		{
			// If there was an error subscribing to notifications, notify the callback:
			if (aError)
			{
				return aOnAlarm(aError, -1, false, {}, {});
			}
		}
	);
}





void Connection::capturePicture(int aChannel, PictureCallback aOnFinish)
{
	nlohmann::json js =
	{
		{"Name", "OPSNAP"},
		{"OPSNAP",
			{
				{ "Channel", aChannel },
			},
		},
	};
	queueCommandRaw(CommandType::NetSnap_Req, CommandType::NetSnap_Resp, js.dump(),
		[aOnFinish](const std::error_code & aError, const char * aData, size_t aSize)
		{
			if (aError)
			{
				return aOnFinish(aError, aData, aSize);
			}

			// Some firmwares return JSON error, others return raw binary data. Try to parse to see if there's an error:
			if (aSize < 500)
			{
				auto j = nlohmann::json::parse(aData, aData + aSize, nullptr, false);
				if (!j.is_discarded())
				{
					auto itr = j.find("Ret");
					if ((itr != j.end()) && (itr->is_number()))
					{
						// An error was found, report it:
						return aOnFinish(make_error_code(static_cast<Error>(*itr)), nullptr, 0);
					}
				}
			}

			// Probably a binary blob representing the picture, call the callback:
			aOnFinish(aError, aData, aSize);
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





void Connection::queueCommandRaw(
	CommandType aCommandType,
	CommandType aExpectedResponseType,
	const std::string & aPayload,
	RawDataCallback aOnFinish
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





void Connection::queueCommand(
	CommandType aCommandType,
	CommandType aExpectedResponseType,
	const std::string & aPayload,
	JsonCallback aOnFinish
)
{
	return queueCommandRaw(aCommandType, aExpectedResponseType, aPayload,
		[self = selfPtr(), aOnFinish](const std::error_code & aErr, const char * aData, size_t aSize)
		{
			if (aErr)
			{
				return aOnFinish(aErr, {});
			}

			// Parse the JSON from the response:
			auto j = nlohmann::json::parse(aData, aData + aSize, nullptr, false);
			if (j.is_discarded())
			{
				return self->disconnected();
			}

			// Remember the session ID:
			auto itr = j.find("SessionID");
			if ((itr != j.end()) && (itr->is_number()))
			{
				self->mSessionID = itr->get<uint32_t>();
			}

			// Call the callback, with error based on the "Ret" field:
			itr = j.find("Ret");
			if ((itr == j.end()) || (!itr->is_number()))
			{
				return aOnFinish(make_error_code(Error::ResponseMissingExpectedField), j);
			}
			else
			{
				if (*itr == Error::Success)
				{
					return aOnFinish({}, j);
				}
				else
				{
					return aOnFinish(make_error_code(static_cast<Error>(*itr)), j);
				}
			}
		}
	);
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





void Connection::notifyAlarm(const char * aData, size_t aSize)
{
	// Typical alarm data:
	// { "AlarmInfo" : { "Channel" : 0, "Event" : "VideoMotion", "StartTime" : "2023-03-02 23:54:59", "Status" : "Stop" }, "Name" : "AlarmInfo", "SessionID" : "0x13" }
	auto onAlarm = mOnAlarm;
	if (!onAlarm)
	{
		return;
	}

	// Parse the JSON from the response:
	auto j = nlohmann::json::parse(aData, aData + aSize, nullptr, false);
	if (j.is_discarded())
	{
		return;
	}

	// Remember the session ID:
	auto itr = j.find("SessionID");
	if ((itr != j.end()) && (itr->is_number()))
	{
		mSessionID = itr->get<uint32_t>();
	}

	// Check that all the required fields are present:
	itr = j.find("AlarmInfo");
	if ((itr == j.end()) || (!itr->is_object()))
	{
		return onAlarm(make_error_code(Error::ResponseMissingExpectedField), -1, false, {}, j);
	}
	const auto & ai = *itr;
	auto itrCh = ai.find("Channel");
	auto itrEvt = ai.find("Event");
	auto itrS = ai.find("Status");
	if (
		(itrCh == ai.end())  || !itrCh->is_number() ||
		(itrEvt == ai.end()) || !itrEvt->is_string() ||
		(itrS == ai.end())   || !itrS->is_string()
	)
	{
		return onAlarm(make_error_code(Error::ResponseMissingExpectedField), -1, false, {}, j);
	}

	return onAlarm({}, *itrCh, *itrS == "Start", *itrEvt, j);
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

		// Find the corresponding callback that is waiting in the queue:
		auto messageType = parseUint16(mIncomingData.data() + start + 14);
		if (messageType == static_cast<uint16_t>(CommandType::Alarm_Req))
		{
			// Special handling for Alarm packets, they have no callback in mIncomingQueue
			notifyAlarm(mIncomingData.data() + start + 20, payloadLength);
		}
		else
		{
			RawDataCallback callback = nullptr;
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
				callback({}, mIncomingData.data() + start + 20, payloadLength);
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
	for (const auto & item: incomingQueue)
	{
		std::get<1>(item)(asio::error::eof, nullptr, 0);
	}
}

}  // namespace NetSurveillancePp
