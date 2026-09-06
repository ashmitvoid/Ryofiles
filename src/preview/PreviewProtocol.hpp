// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QtGlobal>

namespace PreviewProtocol {

inline constexpr qsizetype kMaxRequestLineBytes = 64 * 1024;
inline constexpr qsizetype kMaxProtocolBufferBytes = 4 * 1024 * 1024;
inline constexpr int kMaxPublishedImageDimension = 4096;
inline constexpr qint64 kMaxPublishedImageBytes = 64LL * 1024 * 1024;

} // namespace PreviewProtocol
