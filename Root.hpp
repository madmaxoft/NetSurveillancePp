#pragma once

#include <asio.hpp>





namespace NetSurveillancePp
{





/** The singleton that houses the asio's io_context and the executor threads. */
class Root
{
public:

	/** Returns the single instance of this class, already initialized. */
	static Root & instance();

	/** Returns the asio's io_context to be used by the objects within the library. */
	asio::io_context & ioContext()
	{
		return mIoContext;
	}


private:

	/** The asio's io_context to be used by the objects within the library. */
	asio::io_context mIoContext;

	/** The work guard object that keeps Asio running even if there is no other current IO queued. */
	asio::executor_work_guard<asio::io_context::executor_type> mWorkGuard;

	/** The worker threads in which asio's asynchronous processing is performed. */
	std::vector<std::shared_ptr<std::thread>> mWorkerThreads;


	/** Constructs the single instance.
	Initializes the asio's io_context and starts a single worker thread for the processing. */
	Root();

	~Root();
};

}  // namespace NetSurveillancePp
