/* CoreAudioBaseTypes.h layouts (public). */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
typedef struct { Float64 mMinimum, mMaximum; } AudioValueRange;
typedef UInt32 AudioFormatID; typedef UInt32 AudioFormatFlags;
typedef struct {
	Float64 mSampleRate; AudioFormatID mFormatID; AudioFormatFlags mFormatFlags; UInt32 mBytesPerPacket, mFramesPerPacket,
	mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved;
} AudioStreamBasicDescription;
typedef struct { SInt16 mSubframes, mSubframeDivisor; UInt32 mCounter, mType, mFlags; SInt16 mHours, mMinutes, mSeconds, mFrames; } SMPTETime;
typedef struct { Float64 mSampleTime; UInt64 mHostTime; Float64 mRateScalar; UInt64 mWordClockTime; SMPTETime mSMPTETime;
	UInt32 mFlags, mReserved; } AudioTimeStamp;
enum { kAudioFormatLinearPCM = 'lpcm' };
enum { kAudioFormatFlagIsFloat = 1u << 0, kAudioFormatFlagIsBigEndian = 1u << 1, kAudioFormatFlagIsSignedInteger = 1u << 2,
	kAudioFormatFlagIsPacked = 1u << 3, kAudioFormatFlagIsNonInterleaved = 1u << 5 };
_Static_assert(sizeof(AudioStreamBasicDescription) == 40, "ASBD"); _Static_assert(sizeof(AudioTimeStamp) == 64, "AudioTimeStamp");
_Static_assert(sizeof(SMPTETime) == 24, "SMPTETime");
