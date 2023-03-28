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

	/** The generic callback used for raw incoming data.
	If the error code specifies an error, the callback must NOT touch aData nor aSize (they may be invalid). */
	using RawDataCallback = std::function<void(const std::error_code & aErr, const char * aData, size_t aSize)>;

	/** The generic callback for incoming data, parsed into a JSON structure. */
	using JsonCallback = std::function<void(const std::error_code &, const nlohmann::json &)>;

	/** The callback for enumerating channel names. */
	using ChannelNamesCallback = std::function<void(const std::error_code &, const std::vector<std::string> &)>;

	/** The callback for listening to device's alarms.
	Called when an alarm trigger starts or ends (or there's an error).
	If aError doesn't indicate success, all the other parameters are undefined.
	If aError is Error::ResponseMissingExpectedField, the aWholeJson param still contains the parsed JSON.
	aEventType is the source of the alarm, typically "VideoMotion". */
	using AlarmCallback = std::function<void(
		const std::error_code & aError,
		int aChannel,
		bool aIsStart,
		const std::string & aEventType,
		const nlohmann::json & aWholeJson
	)>;

	/** The callback for capturing a picture.
	The first param is the error code; if successful, the next two params contain the raw picture data. */
	using PictureCallback = std::function<void(const std::error_code &, const char * aData, size_t aSize)>;

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

		SysInfo_Req  = 1020,
		SysInfo_Resp = 1021,

		// Config:
		ConfigSet_Req                = 1040,
		ConfigSet_Resp               = 1041,
		ConfigGet_Req                = 1042,
		ConfigGet_Resp               = 1043,
		DefaultConfigGet_Req         = 1044,
		DefaultConfigGet_Resp        = 1045,
		ConfigChannelTitleSet_Req    = 1046,
		ConfigChannelTitleSet_Resp   = 1047,
		ConfigChannelTitleGet_Req    = 1048,
		ConfigChannelTitleGet_Resp   = 1049,
		ConfigChannelTileDotSet_Req  = 1050,
		ConfigChannelTileDotSet_Resp = 1051,

		SystemDebug_Req  = 1052,
		SystemDebug_Resp = 1053,

		AbilityGet_Req  = 1360,
		AbilityGet_Resp = 1361,

		// PTZ control:
		Ptz_Req = 1400,
		Ptz_Resp = 1401,

		// Monitor (current video playback):
		Monitor_Req       = 1410,
		Monitor_Resp      = 1411,
		Monitor_Data      = 1412,
		MonitorClaim_Req  = 1413,
		MonitorClaim_Resp = 1414,

		// Playback:
		Play_Req = 1420,
		Play_Resp = 1421,
		Play_Data = 1422,
		Play_Eof = 1423,
		PlayClaim_Req = 1424,
		PlayClaim_Resp = 1425,
		DownloadData = 1426,

		// Intercom:
		Talk_Req = 1430,
		Talk_Resp = 1431,
		TalkToNvr_Data = 1432,
		TalkFromNvr_Data = 1433,
		TalkClaim_Req = 1434,
		TalkClaim_Resp = 1435,

		// File search:
		FileSearch_Req         = 1440,
		FileSearch_Resp        = 1441,
		LogSearch_Req          = 1442,
		LogSearch_Resp         = 1443,
		FileSearchByTime_Req   = 1444,
		FileSearchByTyime_Resp = 1445,

		// System management:
		SysMgr_Req     = 1450,
		SysMgr_Resp    = 1451,
		TimeQuery_Req  = 1452,
		TimeQuery_Resp = 1453,

		// Disk management:
		DiskMgr_Req  = 1460,
		DiskMgr_Resp = 1461,

		// User management:
		FullAuthorityListGet_Req  = 1470,
		FullAuthorityListGet_Resp = 1471,
		UsersGet_Req              = 1472,
		UsersGet_Resp             = 1473,
		GroupsGet_Req             = 1474,
		GroupsGet_Resp            = 1475,
		AddGroup_Req              = 1476,
		AddGroup_Resp             = 1477,
		ModifyGroup_Req           = 1478,
		ModifyGroup_Resp          = 1479,
		DeleteGroup_Req           = 1480,
		DeleteGroup_Resp          = 1481,
		AddUser_Req               = 1482,
		AddUser_Resp              = 1483,
		ModifyUser_Req            = 1484,
		ModifyUser_Resp           = 1485,
		DeleteUser_Req            = 1486,
		DeleteUser_Resp           = 1487,
		ModifyPassword_Req        = 1488,
		ModifyPassword_Resp       = 1489,

		// Alarm reporting:
		Guard_Req          = 1500,
		Guard_Resp         = 1501,
		Unguard_Req        = 1502,
		Unguard_Resp       = 1503,
		Alarm_Req          = 1504,
		Alarm_Resp         = 1505,
		NetAlarm_Req       = 1506,
		NetAlarm_Resp      = 1507,
		AlarmCenterMsg_Req = 1508,

		// SysUpgrade:
		SysUpgrade_Req      = 1520,
		SysUpgrade_Resp     = 1521,
		SysUpgradeData_Req  = 1522,
		SysUpgradeData_Resp = 1523,
		SysUpgradeProgress  = 1524,
		SysUpgradeInfo_Req  = 1525,
		SysUpgradeInfo_Resp = 1526,

		// Capture control:
		NetSnap_Req    = 1560,
		NetSnap_Resp   = 1561,
		SetIFrame_Req  = 1562,
		SetIFrame_Resp = 1563,

		// Time sync:
		SyncTime_Req  = 1590,
		SyncTime_Resp = 1591,
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
		JsonCallback aOnFinish
	);

	/** Asynchronously queries the channel names.
	Most devices require logging in first, before enumerating the channels (use connectAndLogin()).
	If successful, calls the callback with the channel names.
	On error, calls the callback with an error code and empty channel names. */
	void getChannelNames(ChannelNamesCallback aOnFinish);

	/** Installs an async alarm monitor.
	The callback is called whenever the device reports an alarm start or stop event.
	Only one monitor can be installed at a time, setting another one overwrites the previous one. */
	void monitorAlarms(Connection::AlarmCallback aOnAlarm);

	/** Asynchronously captures a picture from the specified channel. */
	void capturePicture(int aChannel, PictureCallback aOnFinish);


protected:

	/** The session ID, assigned by the device.
	Zero if not yet set. */
	std::atomic<uint32_t> mSessionID;

	/** The queue of expected response types and their completion handlers waiting for received data.
	Protected against multithreaded access by mMtxTransfer. */
	std::vector<std::pair<CommandType, RawDataCallback>> mIncomingQueue;

	/** The sequence counter for outgoing packets. */
	std::atomic<uint32_t> mSequence;

	/** The AliveInterval received from the device in the Login_Resp packet.
	Specifies the interval, in seconds, between the KeepAlive packets required by the device. */
	int mAliveInterval;

	/** The ASIO timer used for seinding KeepAlive requests. */
	asio::steady_timer mKeepAliveTimer;

	/** The callbacks to call upon receiving an alarm.
	May be nullptr (-> don't call anything, default). */
	AlarmCallback mOnAlarm;


	/** Protected constructor, we want the clients to use std::shared_ptr for owning this object.
	Instead of a constructor, the clients should call create(). */
	Connection();

	/** Returns a correctly typed shared_ptr to self.
	Equivalent to shared_from_this(), but typed correctly for this class. */
	std::shared_ptr<Connection> selfPtr() { return std::static_pointer_cast<Connection>(shared_from_this()); }

	/** Reads the login response, sets up the session ID and keepalives, calls the finish handler.
	If an error is reported, only farwards the error to the finish handler.
	Called by ASIO when a login response has been received. */
	void onLoginResp(const std::error_code & aError, const nlohmann::json & aResponse, JsonCallback aOnFinish);

	void onGetChannelNamesResp(const std::error_code & aError, const nlohmann::json & aResponse, ChannelNamesCallback anFinish);

	/** Queues a KeepAlive request and re-schedules the timer again.
	Called by ASIO periodically (through mKeepAliveTimer). */
	void onKeepAliveTimer(const std::error_code & aError);

	/** Returns the session ID formatted as a hex number, with "0x" prefix (as is often used in the protocol). */
	std::string sessionIDHexStr() const;

	/** Puts the specified command to the send queue to be sent async.
	Once the reply for the command is received, calls the callback from an ASIO worker thread.
	The received data is handed to the callback as-is, with no parsing whatsoever. */
	void queueCommandRaw(
		CommandType aCommandType,
		CommandType aExpectedResponseType,
		const std::string & aPayload,
		RawDataCallback aOnFinish
	);

	/** Puts the specified command to the send queue to be sent async.
	Once the reply for the command is received, calls the callback from an ASIO worker thread.
	The received data is first parsed as JSON, then handed to the callback.
	If parsing the data fails, the connection gets disconnected. */
	void queueCommand(
		CommandType aCommandType,
		CommandType aExpectedResponseType,
		const std::string & aPayload,
		JsonCallback aOnFinish
	);

	/** Serializes the specified command into the on-wire format. */
	std::vector<char> serializeCommand(CommandType aCommandType, const std::string & aPayload);

	/** If an alarm monitor is installed, calls its callback with the parsed data.
	Silently ignored if no alarm monitor is installed. */
	void notifyAlarm(const char * aData, size_t aSize);

	/** Parses mIncomingData for any incoming packets, processes them and removes them from mIncomingData / mIncomingDataSize.
	Implements the functionality used by the underlying TcpConnection. */
	virtual void parseIncomingPackets() override;

	/** Notifies all waiting handlers that the socket has closed.
	Implements the functionality used by the underlying TcpConnection. */
	virtual void disconnected() override;
};





}  // namespace NetSurveillancePp
