#include "TcpConnection.hpp"
#include "Root.hpp"





namespace NetSurveillancePp
{





using LockGuard = std::lock_guard<std::recursive_mutex>;





TcpConnection::TcpConnection():
	mResolver(Root::instance().ioContext()),
	mSocket(Root::instance().ioContext()),
	mIsOutgoing(false),
	mIncomingDataSize(0),
	mIsConnected(false)
{
}





void TcpConnection::connect(
	const std::string & aHostName,
	uint16_t aPort,
	std::function<void(const std::error_code &)> aOnFinish
)
{
	mResolver.async_resolve(aHostName, std::to_string(aPort),
		[self = shared_from_this(), aOnFinish](const std::error_code & aError, asio::ip::tcp::resolver::results_type aResults)
		{
			if (aError)
			{
				aOnFinish(aError);
				return;
			}
			asio::async_connect(self->mSocket, aResults, 
				[self, aOnFinish](const std::error_code & aError, asio::ip::tcp::endpoint const & aEndpoint)
				{
					if (aError)
					{
						aOnFinish(aError);
						return;
					}
					self->mIsConnected = true;
					self->queueRead();
					aOnFinish({});
				}
			);
		}
	);
}





void TcpConnection::send(const std::vector<char> & aData)
{
	{
		LockGuard lg(mMtxTransfer);
		mOutgoingQueue.insert(mOutgoingQueue.end(), aData.begin(), aData.end());
		if (mIsOutgoing)
		{
			return;
		}
	}
	writeNextQueueItem();
}





void TcpConnection::disconnect()
{
	std::error_code err;
	mSocket.shutdown(asio::ip::tcp::socket::shutdown_both, err);
	mSocket.close(err);
}





void TcpConnection::queueRead()
{
	mSocket.async_read_some(
		asio::buffer(mIncomingData.data() + mIncomingDataSize, mIncomingData.size() - mIncomingDataSize),
		[self = shared_from_this()](const std::error_code & aError, std::size_t aNumBytes)
		{
			self->onRead(aError, aNumBytes);
		}
	);
}





void TcpConnection::onWritten(const std::error_code & aError)
{
	if (aError)
	{
		disconnected();
		return;
	}
	LockGuard lg(mMtxTransfer);
	writeNextQueueItem();
}





void TcpConnection::onRead(const std::error_code & aError, std::size_t aNumBytes)
{
	if (aError)
	{
		disconnected();
		return;
	}

	// Process the incoming data:
	mIncomingDataSize += aNumBytes;
	parseIncomingPackets();

	// Read more:
	queueRead();
}





void TcpConnection::writeNextQueueItem()
{
	LockGuard lg(mMtxTransfer);

	// If there's no more data to send, bail out:
	if (mOutgoingQueue.empty())
	{
		mIsOutgoing = false;
		return;
	}

	// Start writing the data through ASIO:
	std::swap(mOutgoingData, mOutgoingQueue);
	mIsOutgoing = true;
	mOutgoingQueue.clear();
	asio::async_write(mSocket, asio::buffer(mOutgoingData), std::bind(&TcpConnection::onWritten, this, std::placeholders::_1));
}

}  // namespace NetSurveillancePp
