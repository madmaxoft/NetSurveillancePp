#include "Error.hpp"





namespace NetSurveillancePp
{





const char * NetSurveillancePp::ErrorCategoryImpl::name() const noexcept
{
	return "NetSurveillancePp";
}





std::string ErrorCategoryImpl::message(int aErrorValue) const
{
	switch (static_cast<Error>(aErrorValue))
	{
		case Error::UnknownError: return "Unknown error";
		case Error::Unsupported: return "Unsupported";
		case Error::IllegalRequest: return "Illegal request";
		case Error::UserAlreadyLoggedIn: return "User already logged in";
		case Error::UserNotLoggedIn: return "User not logged in";
		case Error::BadUsernameOrPassword: return "Bad username or password";
		case Error::NoPermission: return "No permission";
		case Error::Timeout: return "Timeout";
		case Error::SearchFailed: return "Search failed";
		case Error::SearchSuccessReturnAll: return "Search successful, returned all files";
		case Error::SearchSuccessReturnSome: return "Search successful, returned some files";
		case Error::UserAlreadyExists: return "User already exists";
		case Error::UserDoesNotExist: return "User doesn't exist";
		case Error::GroupAlreadyExists: return "Group already exists";
		case Error::GroupDoesNotExist: return "Group doesn't exist";
		case Error::MessageFormatError: return "Message format error";
		case Error::PtzProtocolNotSet: return "PTZ protocol not set";
		case Error::NoFileFound: return "No file found";
		case Error::ConfiguredToEnable: return "Configured to enable";
		case Error::DigitalChannelNotConnected: return "Digital channel not connected";
		case Error::SuccessNeedRestart: return "Success, the device needs to be restarted";
		case Error::UserNotLoggedIn2: return "User not logged in (202)";
		default: return "Unknown error";
	}
	return {};
}





bool ErrorCategoryImpl::equivalent(const std::error_code & aCode, int aCondition) const noexcept
{
	return false;
}





const std::error_category & ErrorCategory()
{
	static ErrorCategoryImpl theInstance;
	return theInstance;
}






std::error_code make_error_code(NetSurveillancePp::Error aError)
{
	return std::error_code(static_cast<int>(aError), NetSurveillancePp::ErrorCategory());
}

}  // namespace NetSurveillancePp
