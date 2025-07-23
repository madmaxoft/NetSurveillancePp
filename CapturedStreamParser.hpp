#pragma once

#include <functional>
#include <string>





namespace NetSurveillancePp
{





/** Parses the "Captured stream" data (either live video, or a remote playback)
and calls the individual handlers when their respective frame is encountered. */
class CapturedStreamParser
{
	using FrameCallback = std::function<void(const void * aData, size_t aSize)>;

	// The individual callbacks:
	FrameCallback mOnVideoIFrame;
	FrameCallback mOnVideoPFrame;
	FrameCallback mOnAudioFrame;
	FrameCallback mOnImageFrame;
	FrameCallback mOnVehicleInfoFrame;

	/** The data for an incomplete frame from the previous call to parse(). */
	std::string mLeftoverFrameData;


public:

	/** Creates a new instance with the specified callbacks. */
	CapturedStreamParser(
		FrameCallback aOnVideoIFrame,
		FrameCallback mOnVideoPFrame,
		FrameCallback mOnAudioFrame = {},
		FrameCallback mOnImageFrame = {},
		FrameCallback mOnVehicleInfoFrame = {}
	);

	/** Parses the specified next piece of data, calling the callbacks accordingly.
	Any leftover frame is kept in mLeftoverFrameData for the next call.
	Returns true if there's no leftover data, false otherwise.
	Throws a std::runtime_error if there is a non-recoverable error in the data stream. */
	bool parse(const void * aData, size_t aSize);

	/** Returns whether there is any leftover data (part of the next frame) in the parser. */
	bool hasLeftoverData() const { return !mLeftoverFrameData.empty(); }
};





}  // namespace NetSurveillancePp
