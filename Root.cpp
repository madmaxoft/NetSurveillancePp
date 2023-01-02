#include "Root.hpp"




using namespace NetSurveillancePp;





Root & Root::instance()
{
	static Root theInstance;
	return theInstance;
}




Root::Root():
	mIoContext(),
	mWorkGuard(asio::make_work_guard(mIoContext))
{
	mWorkerThreads.emplace_back(new std::thread([this](){mIoContext.run();}));
}





Root::~Root()
{
	mWorkGuard.reset();
	mIoContext.post([this](){mWorkGuard.reset(); });
	mIoContext.stop();
	for (const auto & thr: mWorkerThreads)
	{
		thr->join();
	}
}