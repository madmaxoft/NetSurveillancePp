#include "Recorder.hpp"

#include <asio.hpp>
#include "Root.hpp"
#include "Error.hpp"





namespace NetSurveillancePp
{





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
		[self = shared_from_this(), aUserName, aPassword, aOnFinish](const std::error_code & aError)
		{
			if (aError)
			{
				return aOnFinish(aError);
			}
			self->mMainConnection->login(aUserName, aPassword,
				[self, aOnFinish](const std::error_code & aError, const nlohmann::json & aResponse)
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






void Recorder::capturePicture(int aChannel, Connection::PictureCallback aOnFinish)
{
	auto conn = mMainConnection;
	if (conn == nullptr)
	{
		aOnFinish(make_error_code(Error::NoConnection), nullptr, 0);
		return;
	}
	conn->capturePicture(aChannel, std::move(aOnFinish));
}





}  // namespace NetSurveillancePp
