#include "CapturedStreamParser.hpp"
#include "ByteStream.hpp"

#include <stdexcept>




/*
NOTE: The format of the CapturedStream is documented in
https://github.com/madmaxoft/python-dvr/blob/master/doc/%E7%A0%81%E6%B5%81%E5%B8%A7%E6%A0%BC%E5%BC%8F%E6%96%87%E6%A1%A3.pdf

The documentation is in Chinese and is incomplete, but it is sufficient at least for extracting video frames.
*/





namespace
{

/** The maximum number of bytes in the leftover buffer (max allowed frame size).
If the leftover bytes overflow this, an error is reported. */
static constexpr size_t MAX_LEFTOVER_SIZE = 1024 * 1024;





/** The type of the frame in the data.
The values correspond  */
enum FrameType: unsigned char
{
	VideoIFrame = 0xfc,
	VideoPFrame = 0xfd,
	AudioFrame = 0xfa,
	ImageFrame = 0xfe,
	VehicleInfoFrame = 0xf9,
};





/** The codec that was used for encoding the video. */
enum VideoFrameCodec: unsigned char
{
	Mpg4 = 0x01,
	H264 = 0x02,
	H265 = 0x03,
};





/** The codec that was used for encoding the audio. */
enum AudioFrameCodec: unsigned char
{
	G711a = 0x0e,
};





/** The audio sample rate. */
enum AudioSampleRate
{
	Hz8000 = 0x02,
};





/** The codec that was used for encoding the image. */
enum ImageFrameCodec: unsigned char
{
	Jpg = 0x00,
};





/** The timestamp of the frame. */
struct DateTime
{
	// TODO
	unsigned mValue;
};





/** The frame header, as an amalgamation of all the possible fields in the various frame types' headers,
as well as other housekeeping info. */
struct FrameHeader
{
	/** Size of the header, in bytes, including the initial 0x00 0x00 0x01 triplet. */
	size_t mHeaderLength;

	/** Size of the actual payload data, as parsed from the data. */
	size_t mDataLength;

	/** The type of the encountered frame, as parsed from the data. */
	FrameType mFrameType;

	union
	{
		// Video I-frame fields:
		struct
		{
			VideoFrameCodec mVideoFrameCodec;
			unsigned mVideoWidth;
			unsigned mVideoHeight;
			unsigned char mVideoFrameRate;
			DateTime mVideoDateTime;
		};

		// Audio frame fields:
		struct
		{
			AudioFrameCodec mAudioFrameCodec;
			AudioSampleRate mAudioSampleRate;
		};

		// Image fields:
		struct
		{
			ImageFrameCodec mImageFrameCodec;
			unsigned mImageWidth;
			unsigned mImageHeight;
			DateTime mImageDateTime;
		};
	};
};





/** Tries to parse the frame header from the specified data.
Returns {true, header} if the header parsed successfully, {false, ?} otherwise.
Throws a std::runtime_error if there's a non-recoverable error in the stream
(e.g. missing initial triplets or unknown frame type). */
std::pair<bool, FrameHeader> tryParseFrameHeader(const char * aData, size_t aSize)
{
	static const FrameHeader unusedHeader = {};
	if (aSize < 8)
	{
		// Not enough bytes yet to have even the smallest frame:
		return {false, unusedHeader};
	}

	// Check the initial triplet:
	if ((aData[0] != 0x00) || (aData[1] != 0x00) || (aData[2] != 0x01))
	{
		throw std::runtime_error("Invalid initial triplet.");
	}

	// The next byte specifies the frame type:
	auto data = reinterpret_cast<const unsigned char *>(aData);
	FrameHeader hdr;
	hdr.mFrameType = static_cast<FrameType>(data[3]);
	switch (hdr.mFrameType)
	{
		case FrameType::VideoIFrame:
		{
			hdr.mHeaderLength = 16;
			if (aSize < 16)
			{
				return {false, unusedHeader};
			}
			hdr.mVideoFrameCodec = static_cast<VideoFrameCodec>(data[4] & 0x0f);
			hdr.mVideoFrameRate = static_cast<unsigned char>(data[5] & 0x1f);
			hdr.mVideoWidth = static_cast<unsigned>(data[6]) | (static_cast<unsigned>(data[4] & 0x30) << 4);
			hdr.mVideoHeight = static_cast<unsigned>(data[7]) | (static_cast<unsigned>(data[4] & 0xc0) << 2);
			// TODO: DateTime @ data + 8
			hdr.mDataLength = ByteStream::parseUint32(data + 12);
			return {true, hdr};
		}
		case VideoPFrame:
		{
			hdr.mHeaderLength = 8;
			hdr.mDataLength = ByteStream::parseUint32(data + 4);
			return {true, hdr};
		}
		case AudioFrame:
		{
			hdr.mHeaderLength = 8;
			hdr.mAudioFrameCodec = static_cast<AudioFrameCodec>(data[4] & 0x0f);
			hdr.mAudioSampleRate = static_cast<AudioSampleRate>(data[5]);
			hdr.mDataLength = ByteStream::parseUint16(data + 6);
			return {true, hdr};
		}
		case ImageFrame:
		{
			hdr.mHeaderLength = 16;
			hdr.mImageFrameCodec = static_cast<ImageFrameCodec>(data[4] & 0x0f);
			hdr.mImageWidth = 8 * (static_cast<unsigned>(data[6]) | (static_cast<unsigned>(data[4] & 0x30) << 4));
			hdr.mImageHeight = 8 * (static_cast<unsigned>(data[7]) | (static_cast<unsigned>(data[4] & 0xc0) << 2));
			// TODO: DateTime @ data + 8
			hdr.mDataLength = ByteStream::parseUint32(data + 12);
			return {true, hdr};
		}
		case VehicleInfoFrame:
		{
			// TODO: There's very little docs about what this actually is and none of my units emit this.
			hdr.mHeaderLength = 8;
			hdr.mDataLength = ByteStream::parseUint16(data + 6);
			return {true, hdr};
		}
	}
	throw std::runtime_error("Unknown frame type");
}



}  // Anonymous namespace





namespace NetSurveillancePp
{





CapturedStreamParser::CapturedStreamParser(
	FrameCallback aOnVideoIFrame,
	FrameCallback aOnVideoPFrame,
	FrameCallback aOnAudioFrame,
	FrameCallback aOnImageFrame,
	FrameCallback aOnVehicleInfoFrame
):
	mOnVideoIFrame(aOnVideoIFrame),
	mOnVideoPFrame(aOnVideoPFrame),
	mOnAudioFrame(aOnAudioFrame),
	mOnImageFrame(aOnImageFrame),
	mOnVehicleInfoFrame(aOnVehicleInfoFrame)
{
	// Nothing explicit needed yet
}





bool CapturedStreamParser::parse(const void * aData, size_t aSize)
{
	// Append the data to the buffer:
	if (aSize + mLeftoverFrameData.size() > MAX_LEFTOVER_SIZE)
	{
		throw std::runtime_error("Too much leftover data.");
	}
	mLeftoverFrameData.append(reinterpret_cast<const char *>(aData), aSize);

	while (true)
	{
		// Try parsing the frame header:
		auto [isValid, hdr] = tryParseFrameHeader(mLeftoverFrameData.data(), mLeftoverFrameData.size());
		if (!isValid)
		{
			return !mLeftoverFrameData.empty();
		}
		if (hdr.mHeaderLength + hdr.mDataLength > mLeftoverFrameData.size())
		{
			// Not enough data bytes, leave for later:
			return false;
		}

		// Call the callback based on the frame type:
		switch (hdr.mFrameType)
		{
			case FrameType::VideoIFrame:
			{
				if (mOnVideoIFrame)
				{
					mOnVideoIFrame(mLeftoverFrameData.data() + hdr.mHeaderLength, hdr.mDataLength);
				}
				break;
			}
			case VideoPFrame:
			{
				if (mOnVideoPFrame)
				{
					mOnVideoPFrame(mLeftoverFrameData.data() + hdr.mHeaderLength, hdr.mDataLength);
				}
				break;
			}
			case AudioFrame:
			{
				if (mOnAudioFrame)
				{
					mOnAudioFrame(mLeftoverFrameData.data() + hdr.mHeaderLength, hdr.mDataLength);
				}
				break;
			}
			case ImageFrame:
			{
				if (mOnImageFrame)
				{
					mOnImageFrame(mLeftoverFrameData.data() + hdr.mHeaderLength, hdr.mDataLength);
				}
				break;
			}
			case VehicleInfoFrame:
			{
				if (mOnVehicleInfoFrame)
				{
					mOnVehicleInfoFrame(mLeftoverFrameData.data() + hdr.mHeaderLength, hdr.mDataLength);
				}
				break;
			}
		}

		// Erase the frame from the buffer:
		mLeftoverFrameData.erase(0, hdr.mHeaderLength + hdr.mDataLength);
	}

	return !mLeftoverFrameData.empty();
}





}  // namespace NetSurveillancePp
