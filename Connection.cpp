#include "Connection.hpp"

#include <fmt/format.h>

#include "ByteStream.hpp"
#include "Root.hpp"
#include "SofiaHash.hpp"
#include "Error.hpp"

#define DEBUGLOG(fmtString, ...) \
	if (mDebugFile != nullptr) \
	{ \
		*mDebugFile << fmt::format(FMT_STRING(fmtString), __VA_ARGS__); \
	}






namespace NetSurveillancePp
{





using LockGuard = std::lock_guard<std::recursive_mutex>;





////////////////////////////////////////////////////////////////////////////////
// Globals:

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





void Connection::copySessionIDFrom(const Connection & aOtherConnection)
{
	assert(mSessionID.load() == 0);  // SessionID hasn't been assigned yet
	mSessionID = aOtherConnection.mSessionID.load();
}





void Connection::connect(
	const std::string & aHostName,
	uint16_t aPort,
	std::function<void(const std::error_code &)> aOnFinish
)
{
	Super::connect(aHostName, aPort, aOnFinish);
}





void Connection::close()
{
	Super::disconnect();
	mOnAlarm = nullptr;
	mOnCapturedStreamData = nullptr;
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





void Connection::getSysInfo(NamedJsonCallback aOnFinish, const std::string & aInfoName)
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





void Connection::getAbility(NamedJsonCallback aOnFinish, const std::string & aAbilityName)
{
	nlohmann::json js =
	{
		{"SessionID", sessionIDHexStr()},
		{"Name",      aAbilityName},
	};
	queueCommand(CommandType::AbilityGet_Req, CommandType::AbilityGet_Resp, js.dump(),
		[aOnFinish, aAbilityName](const std::error_code & aError, const nlohmann::json & aResponse)
		{
			aOnFinish(aError, aAbilityName, aResponse);
		}
	);
}





void Connection::getConfig(NamedJsonCallback aOnFinish, const std::string & aConfigName)
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





void Connection::capturePicture(PictureCallback aOnFinish, int aChannel)
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
		[aOnFinish](const std::error_code & aError, const void * aData, size_t aSize)
		{
			if (aError)
			{
				return aOnFinish(aError, aData, aSize);
			}

			// Some firmwares return JSON error, others return raw binary data. Try to parse to see if there's an error:
			if (aSize < 500)
			{
				auto data = reinterpret_cast<const char *>(aData);
				auto j = nlohmann::json::parse(data, data + aSize, nullptr, false);
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





void Connection::startReceivingMonitorData(
	JsonCallback aOnMonitorClaimed,
	CapturedStreamCallback aOnData,
	const nlohmann::json & aSpec,
	CommandType aReqType,
	CommandType aRespType
)
{
	assert(mOnCapturedStreamData == nullptr);  // Already claimed a stream?
	mOnCapturedStreamData = std::move(aOnData);

	queueCommand(aReqType, aRespType, aSpec.dump(), aOnMonitorClaimed);
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
		[self = selfPtr(), aOnFinish](const std::error_code & aErr, const void * aData, size_t aSize)
		{
			if (aErr)
			{
				return aOnFinish(aErr, {});
			}

			// Parse the JSON from the response:
			auto data = reinterpret_cast<const char *>(aData);
			auto j = nlohmann::json::parse(data, data + aSize, nullptr, false);
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
	ByteStream::writeUint32(res, mSessionID);
	ByteStream::writeUint32(res, seq);
	res.push_back(Protocol::TOTALPKT);
	res.push_back(Protocol::CURRPKT);
	ByteStream::writeUint16(res, static_cast<uint16_t>(aCommandType));
	ByteStream::writeUint32(res, static_cast<uint32_t>(aPayload.size()));
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
	DEBUGLOG("Parsing incoming packets: mIncomingDataSize = {}", mIncomingDataSize);
	size_t start = 0;
	while (mIncomingDataSize - start >= 20)
	{
		// Check if an entire packet is in the queue:
		if (mIncomingData[start] != Protocol::IDENTIFICATION)
		{
			DEBUGLOG("Not a NetSurveillance protocol (header == 0x{:x} instead of 0x{:x}", mIncomingData[start], Protocol::IDENTIFICATION);
			return disconnected();
		}
		auto payloadLength = ByteStream::parseUint32(mIncomingData.data() + start + 16);
		if (mIncomingDataSize - start < payloadLength + Protocol::HeaderLength)
		{
			DEBUGLOG("Not a complete packet (need 0x{:x} bytes, got 0x{:x} in buffer", payloadLength + Protocol::HeaderLength, mIncomingDataSize - start);
			break;
		}

		// Find the corresponding callback that is waiting in the queue:
		auto messageType = ByteStream::parseUint16(mIncomingData.data() + start + 14);
		if (messageType == static_cast<uint16_t>(CommandType::Alarm_Req))
		{
			// Special handling for Alarm packets, they have no callback in mIncomingQueue
			notifyAlarm(mIncomingData.data() + start + 20, payloadLength);
		}
		else if (
			(messageType == static_cast<uint16_t>(CommandType::Monitor_Data)) ||
			(messageType == static_cast<uint16_t>(CommandType::Play_Data))
		)
		{
			if (mOnCapturedStreamData != nullptr)
			{
				mOnCapturedStreamData({}, mIncomingData.data() + start + 20, payloadLength);
			}
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
