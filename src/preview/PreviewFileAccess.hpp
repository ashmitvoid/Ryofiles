// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <memory>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace PreviewFileAccess {

struct OpenedFile {
    std::unique_ptr<QFile> file;
    qint64 size = 0;
    quint64 device = 0;
    quint64 inode = 0;
    qint64 mtimeNs = 0;

    explicit operator bool() const { return file != nullptr; }
};

#ifdef Q_OS_UNIX
inline qint64 mtimeNanoseconds(const struct stat& st) {
#ifdef Q_OS_LINUX
    return static_cast<qint64>(st.st_mtim.tv_sec) * 1'000'000'000LL
        + static_cast<qint64>(st.st_mtim.tv_nsec);
#else
    return static_cast<qint64>(st.st_mtime) * 1'000'000'000LL;
#endif
}

inline bool sameVersion(const struct stat& left, const struct stat& right) {
    return left.st_dev == right.st_dev
        && left.st_ino == right.st_ino
        && left.st_mode == right.st_mode
        && left.st_size == right.st_size
        && mtimeNanoseconds(left) == mtimeNanoseconds(right);
}

inline QString errnoText(const char* action) {
    return QStringLiteral("%1: %2")
        .arg(QString::fromLatin1(action), QString::fromLocal8Bit(std::strerror(errno)));
}

inline OpenedFile openRegularNoFollow(const QString& path, QString* error) {
    OpenedFile result;
    const QFileInfo info(path);
    if (path.isEmpty() || path.contains(QChar::Null) || !info.isAbsolute()) {
        if (error)
            *error = QStringLiteral("Preview path must be an absolute local path");
        return result;
    }

    const QByteArray encoded = QFile::encodeName(path);
    struct stat initial {};
    if (::lstat(encoded.constData(), &initial) != 0) {
        if (error)
            *error = errnoText("Could not inspect preview file");
        return result;
    }
    if (!S_ISREG(initial.st_mode)) {
        if (error)
            *error = QStringLiteral("Preview input is not a regular file");
        return result;
    }

    const int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (error)
            *error = errnoText("Could not open preview file");
        return result;
    }

    struct stat opened {};
    if (::fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode)) {
        const int savedErrno = errno;
        ::close(fd);
        errno = savedErrno;
        if (error)
            *error = errnoText("Could not inspect opened preview file");
        return result;
    }

    struct stat live {};
    if (::lstat(encoded.constData(), &live) != 0 || !sameVersion(opened, live)) {
        ::close(fd);
        if (error)
            *error = QStringLiteral("Preview file changed while it was being opened");
        return result;
    }

    auto file = std::make_unique<QFile>();
    if (!file->open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        if (error)
            *error = QStringLiteral("Could not attach preview file descriptor");
        return result;
    }

    result.size = static_cast<qint64>(opened.st_size);
    result.device = static_cast<quint64>(opened.st_dev);
    result.inode = static_cast<quint64>(opened.st_ino);
    result.mtimeNs = mtimeNanoseconds(opened);
    result.file = std::move(file);
    return result;
}
#else
inline OpenedFile openRegularNoFollow(const QString&, QString* error) {
    if (error)
        *error = QStringLiteral("Preview file access requires Unix no-follow APIs");
    return {};
}
#endif

} // namespace PreviewFileAccess
