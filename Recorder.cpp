#include "Recorder.hpp"

#include <asio.hpp>
#include "Error.hpp"
#include "fmt/format.h"





namespace NetSurveillancePp
{





class CapturedStreamReceiver:
	public Recorder::ICapturedStreamReceiver,
	public std::enable_shared_from_this<CapturedStreamReceiver>
{
	/** Whether this is a LiveVideo or RemotePlayback kind. */
	enum class Kind
	{
		LiveVideo,
		RemotePlayback,
	} mKind;

	std::shared_ptr<Recorder> mRecorder;
	std::shared_ptr<Connection> mMainConnection;
	std::shared_ptr<Connection> mDataConnection;
	Connection::CapturedStreamCallback mOnData;

	// LiveVideo params:
	int mChannel = 0;

	// RemotePlayback params:
	time_t mStartTimeStamp = 0;
	time_t mEndTimeStamp = 0;
	std::string mRemoteFileName;

	/** SP to self, to keep this object alive until the call to stop(). */
	std::shared_ptr<CapturedStreamReceiver> mSelf;


public:

	/** Creates a new LiveVideo instance. */
	CapturedStreamReceiver(
		std::shared_ptr<Recorder> aRecorder,
		std::shared_ptr<Connection> aMainConnection,
		Connection::CapturedStreamCallback aOnData,
		int aChannel
	):
		mKind(Kind::LiveVideo),
		mRecorder(aRecorder),
		mMainConnection(aMainConnection),
		mOnData(aOnData),
		mChannel(aChannel)
	{
	}




	/** Creates a new RemotePlayback instance. */
	CapturedStreamReceiver(
		std::shared_ptr<Recorder> aRecorder,
		std::shared_ptr<Connection> aMainConnection,
		Connection::CapturedStreamCallback aOnData,
		time_t aStartTimeStamp,
		time_t aEndTimeStamp,
		const std::string & aRemoteFileName
	):
		mKind(Kind::RemotePlayback),
		mRecorder(aRecorder),
		mMainConnection(aMainConnection),
		mOnData(aOnData),
		mStartTimeStamp(aStartTimeStamp),
		mEndTimeStamp(aEndTimeStamp),
		mRemoteFileName(aRemoteFileName)
	{
	}




	/** Starts the actual receiving, asynchronously:
		- connects the data connection
		- claims the data connection
		- starts the operation via the main connection
	*/
	void start(
		const std::string & aHostName,
		int aPort
	)
	{
		mSelf = shared_from_this();  // Keep myself alive

		// Connect a new connection for the data stream:
		mDataConnection = Connection::create();
		mDataConnection->connect(aHostName, aPort,
			[self = mSelf](const std::error_code & aError)
			{
				self->onDataConnectionConnected(aError);
			}
		);
	}





	/** Returns the JSON for claiming the data connection. */
	nlohmann::json getClaimJson() const
	{
		switch (mKind)
		{
			case Kind::LiveVideo:
			{
				return
				{
					{"Name", "OPMonitor"},
					{"OPMonitor",
						{
							{"Action", "Claim"},
							{"Parameter",
								{
									{"Channel", fmt::format(FMT_STRING("{}"), mChannel)},
									{"CombinMode", "NONE"},
									{"StreamType", "Main"},
									{"Transport", "TCP"},
								},
							},
						},
					},
				};
			}  // LiveVideo

			case Kind::RemotePlayback:
			{
				return
				{
					{"Name", "OPPlayBack"},
					{"OPPlayBack",
						{
							{"Action", "Claim"},
							{"StartTime", Recorder::formatTimeStamp(mStartTimeStamp)},
							{"EndTime",   Recorder::formatTimeStamp(mEndTimeStamp)},
							{"Parameter",
								{
									{"FileName", mRemoteFileName},
									{"Transport", "TCP"},
								},
							},
						},
					},
				};
			}  // RemotePlayback
		}
		return {};
	}





	nlohmann::json getStartJson() const
	{
		switch (mKind)
		{
			case Kind::LiveVideo:
			{
				return
				{
					{"Name", "OPMonitor"},
					{"SessionID", mMainConnection->sessionIDHexStr()},
					{"OPMonitor",
						{
							{"Action", "Start"},
							{"Parameter",
								{
									{"Channel", fmt::format(FMT_STRING("{}"), mChannel)},
									{"CombinMode", "NONE"},
									{"StreamType", "Main"},
									{"Transport", "TCP"},
								},
							},
						},
					},
				};
			}  // LiveVideo

			case Kind::RemotePlayback:
			{
				return
				{
					{"Name", "OPPlayBack"},
					{"OPPlayBack",
						{
							{"Action", "Start"},
							{"StartTime", Recorder::formatTimeStamp(mStartTimeStamp)},
							{"EndTime",   Recorder::formatTimeStamp(mEndTimeStamp)},
							{"Parameter",
								{
									{"FileName", mRemoteFileName},
									{"Transport", "TCP"},
								},
							},
						},
					},
				};
			}  // RemotePlayback
		}
		return {};
	}





	void onDataConnectionConnected(const std::error_code & aError)
	{
		if (aError)
		{
			return mOnData(aError, nullptr, 0);
		}
		mDataConnection->copySessionIDFrom(*mMainConnection);
		auto reqType  = (mKind == Kind::LiveVideo) ? Connection::CommandType::MonitorClaim_Req  : Connection::CommandType::PlayClaim_Req;
		auto respType = (mKind == Kind::LiveVideo) ? Connection::CommandType::MonitorClaim_Resp : Connection::CommandType::PlayClaim_Resp;
		mDataConnection->startReceivingMonitorData(
			[self = mSelf](const std::error_code & aError2, const nlohmann::json & aResp2)
			{
				self->onMonitorClaimed(aError2, aResp2);
			},
			mOnData,
			getClaimJson(),
			reqType, respType
		);
	}





	void onMonitorClaimed(const std::error_code & aError, const nlohmann::json & aResp)
	{
		if (aError)
		{
			auto resp = aResp.dump();
			close();
			return mOnData(aError, resp.c_str(), resp.size());
		}

		// The data connection is ready to receive data,
		// send a request through the main connection to start the live video:
		auto reqType  = (mKind == Kind::LiveVideo) ? Connection::CommandType::Monitor_Req  : Connection::CommandType::Play_Req;
		auto respType = (mKind == Kind::LiveVideo) ? Connection::CommandType::Monitor_Resp : Connection::CommandType::Play_Resp;
		mMainConnection->queueCommand(reqType, respType, getStartJson().dump(),
			[self = shared_from_this()](const std::error_code & aError2, const nlohmann::json & aResp2)
			{
				if (!aError2)
				{
					return;
				}

				// There was an error while requesting a StartMonitor, abort the whole operation:
				auto resp = aResp2.dump();
				self->close();
				self->mOnData(aError2, resp.c_str(), resp.size());
			}
		);
	}





	virtual void close() override
	{
		mDataConnection->close();
		mSelf.reset();
	}
};





//////////////////////////////////////////////////////////////////////////////
// Recorder:

std::shared_ptr<Recorder> Recorder::create()
{
	return std::shared_ptr<Recorder>(new Recorder);
}





Recorder::~Recorder()
{
	disconnect();
}





Recorder::Recorder():
	mMainConnection(Connection::create())
{
}





void Recorder::connectAndLogin(
	const std::string & aHostName,
	uint16_t aPort,
	const std::string & aUserName,
	const std::string & aPassword,
	std::function<void(const std::error_code &)> aOnFinish
)
{
	mMainConnection->connect(aHostName, aPort,
		[self = shared_from_this(), aHostName, aPort, aUserName, aPassword, aOnFinish](const std::error_code & aError)
		{
			if (aError)
			{
				return aOnFinish(aError);
			}
			self->mHostName = aHostName;
			self->mPort = aPort;
			self->mMainConnection->login(aUserName, aPassword,
				[self, aOnFinish, aUserName, aPassword](const std::error_code & aError, const nlohmann::json & aResponse)
				{
					if (aError)
					{
						return aOnFinish(aError);
					}
					auto itr = aResponse.find("Ret");
					if ((itr == aResponse.end()) || (*itr != 100))
					{
						return aOnFinish(static_cast<Error>(itr->get<int>()));
					}
					// Connected and logged into successfully
					self->mUserName = aUserName;
					self->mPassword = aPassword;
					aOnFinish({});
				}
			);
		}
	);
}





void Recorder::disconnect()
{
	auto conn = mMainConnection;
	if (conn)
	{
		conn->disconnect();
	}
}





void Recorder::getChannelNames(Connection::ChannelNamesCallback aOnFinish)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnFinish(make_error_code(Error::NoConnection), {});
	}
	conn->getChannelNames(aOnFinish);
}





void Recorder::getSysInfo(Connection::NamedJsonCallback aOnFinish, const std::string & aInfoName)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnFinish(make_error_code(Error::NoConnection), {}, {});
	}
	conn->getSysInfo(aOnFinish, aInfoName);
}





void Recorder::getAbility(Connection::NamedJsonCallback aOnFinish, const std::string & aAbilityName)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnFinish(make_error_code(Error::NoConnection), {}, {});
	}
	conn->getAbility(aOnFinish, aAbilityName);
}





void Recorder::getConfig(Connection::NamedJsonCallback aOnFinish, const std::string & aConfigName)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnFinish(make_error_code(Error::NoConnection), {}, {});
	}
	conn->getConfig(aOnFinish, aConfigName);
}





void Recorder::monitorAlarms(Connection::AlarmCallback aOnAlarm)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnAlarm(make_error_code(Error::NoConnection), -1, false, {}, {});
		return;
	}
	conn->monitorAlarms(aOnAlarm);
}






void Recorder::capturePicture(Connection::PictureCallback aOnFinish, int aChannel)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnFinish(make_error_code(Error::NoConnection), nullptr, 0);
		return;
	}
	conn->capturePicture(std::move(aOnFinish), aChannel);
}





Recorder::ICapturedStreamReceiverPtr Recorder::receiveLiveVideo(Connection::CapturedStreamCallback aOnData, int aChannel)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnData(make_error_code(Error::NoConnection), nullptr, 0);
		return nullptr;
	}

	auto res = std::make_shared<CapturedStreamReceiver>(shared_from_this(), conn, aOnData, aChannel);
	res->start(mHostName, mPort);
	return res;
}





Recorder::ICapturedStreamReceiverPtr Recorder::receiveRemotePlayback(
	Connection::CapturedStreamCallback aOnData,
	time_t aStartTimeStamp,
	time_t aEndTimeStamp,
	const std::string & aRemoteFileName
)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnData(make_error_code(Error::NoConnection), nullptr, 0);
		return nullptr;
	}

	auto res = std::make_shared<CapturedStreamReceiver>(shared_from_this(), conn, aOnData, aStartTimeStamp, aEndTimeStamp, aRemoteFileName);
	res->start(mHostName, mPort);
	return res;
}





std::string Recorder::formatTimeStamp(time_t aTimeStamp)
{
	auto timeStamp = std::chrono::system_clock::from_time_t(aTimeStamp);
	auto ymd = std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(timeStamp));
	auto hms = std::chrono::hh_mm_ss(timeStamp - std::chrono::floor<std::chrono::days>(timeStamp));

	return fmt::format(FMT_STRING("{}-{:02}-{:02} {:02}:{:02}:{:02}"),
		static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
		hms.hours().count(), hms.minutes().count(), hms.seconds().count()
	);
}

}  // namespace NetSurveillancePp
