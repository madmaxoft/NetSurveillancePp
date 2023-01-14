#pragma once

#include <system_error>





namespace NetSurveillancePp
{





enum class Error
{
	// Synthetic error codes:
	ResponseMissingExpectedField = 1,  // The response was missing an expected field, required for further communication

	// Error codes reported by the device ("Ret" code in the json):
	UnknownError = 101,
	Unsupported = 102,
	IllegalRequest = 103,
	UserAlreadyLoggedIn = 104,
	UserNotLoggedIn = 105,
	BadUsernameOrPassword = 106,
	NoPermission = 107,
	Timeout = 108,
	SearchFailed = 109,
	SearchSuccessReturnAll  = 110,
	SearchSuccessReturnSome = 111,
	UserAlreadyExists = 112,
	UserDoesNotExist = 113,
	GroupAlreadyExists = 114,
	GroupDoesNotExist = 115,
	MessageFormatError = 117,
	PtzProtocolNotSet = 118,
	NoFileFound = 119,
	ConfiguredToEnable = 120,
	DigitalChannelNotConnected = 121,
	SuccessNeedRestart = 150,
	UserNotLoggedIn2 = 202,
};





class ErrorCategoryImpl:
	public std::error_category
{
public:
	virtual const char * name() const noexcept override;
	virtual std::string message(int aErrorValue) const override;
	virtual bool equivalent(
		const std::error_code & aCode,
		int aCondition
	) const noexcept override;
};

const std::error_category & ErrorCategory();

std::error_code make_error_code(Error aError);


}  // namespace NetSurveillancePp





namespace std
{




template <>
struct is_error_code_enum<NetSurveillancePp::Error>:
	public true_type
{
};




}
