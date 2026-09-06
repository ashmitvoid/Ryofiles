// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QtGlobal>

namespace PreviewProtocol {

inline constexpr qsizetype kMaxRequestLineBytes = 64 * 1024;
inline constexpr qsizetype kMaxProtocolBufferBytes = 4 * 1024 * 1024;
inline constexpr int kMaxPublishedImageDimension = 4096;
inline constexpr qint64 kMaxPublishedImageBytes = 64LL * 1024 * 1024;

inline constexpr qint64 kMaxPdfInputBytes = 512LL * 1024 * 1024;
inline constexpr int kDefaultPdfRenderWidth = 1200;
inline constexpr int kDefaultPdfRenderHeight = 1600;
inline constexpr int kMaxPdfRenderDimension = 2048;
inline constexpr qsizetype kMaxEncodedImageBytes = 2 * 1024 * 1024;

inline constexpr qint64 kMaxMediaInputBytes = 64LL * 1024 * 1024 * 1024;
inline constexpr qint64 kMaxMediaReadBytes = 64LL * 1024 * 1024;
inline constexpr qint64 kMaxMediaProbeBytes = 4LL * 1024 * 1024;
inline constexpr qint64 kMaxMediaAnalyzeUs = 5LL * 1000 * 1000;
inline constexpr int kMediaAvioBufferBytes = 32 * 1024;
inline constexpr int kMaxMediaMetadataChars = 512;
inline constexpr int kMaxMediaSourceDimension = 8192;
inline constexpr qint64 kMaxMediaSourcePixels = 40LL * 1024 * 1024;
inline constexpr int kDefaultMediaPosterWidth = 960;
inline constexpr int kDefaultMediaPosterHeight = 540;
inline constexpr int kMaxMediaPosterDimension = 1280;
inline constexpr int kMaxMediaPackets = 512;

inline constexpr qint64 kMaxFontInputBytes = 32LL * 1024 * 1024;
inline constexpr int kDefaultFontRenderWidth = 960;
inline constexpr int kDefaultFontRenderHeight = 420;
inline constexpr int kMaxFontRenderDimension = 1280;
inline constexpr int kDefaultFontPixelSize = 64;
inline constexpr int kMaxFontPixelSize = 96;
inline constexpr int kMaxFontSampleChars = 96;
inline constexpr int kMaxFontWritingSystems = 16;

inline constexpr qint64 kMaxImageMetadataInputBytes = 64LL * 1024 * 1024;
inline constexpr int kMaxImageMetadataChars = 256;
inline constexpr qint64 kMaxAnimationInputBytes = 64LL * 1024 * 1024;
inline constexpr int kMaxAnimationSourceDimension = 4096;
inline constexpr qint64 kMaxAnimationSourcePixels = 16LL * 1024 * 1024;
inline constexpr int kDefaultAnimationFrameWidth = 960;
inline constexpr int kDefaultAnimationFrameHeight = 720;
inline constexpr int kMaxAnimationFrameDimension = 1024;
inline constexpr int kMaxAnimationFrames = 120;
inline constexpr int kMinAnimationDelayMs = 50;
inline constexpr int kMaxAnimationDelayMs = 500;
inline constexpr int kMaxAnimationPlayMs = 30'000;

} // namespace PreviewProtocol
