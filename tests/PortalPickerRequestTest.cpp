// SPDX-License-Identifier: GPL-3.0-only

#include "portal/PortalPickerRequest.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class PortalPickerRequestTest final : public QObject {
    Q_OBJECT

private:
    static QByteArray encodedPath(const QString& path) {
        QByteArray encoded = QFile::encodeName(path);
        encoded.append('\0');
        return encoded;
    }

    static QByteArray encodedLeaf(const QString& name) {
        QByteArray encoded = QFile::encodeName(name);
        encoded.append('\0');
        return encoded;
    }

    static void writeFile(const QString& path, const QByteArray& data = QByteArrayLiteral("x")) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(data), data.size());
    }

    static QVariant filterVariant(
        const QString& name,
        const QList<PortalFilterCondition>& conditions) {
        QVariantList serializedConditions;
        for (const PortalFilterCondition& condition : conditions) {
            serializedConditions.push_back(QVariantMap{
                {QStringLiteral("type"), condition.type},
                {QStringLiteral("pattern"), condition.pattern},
            });
        }
        return QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("conditions"), serializedConditions},
        };
    }

private slots:
    void decodesOnlyStrictLocalNullTerminatedPaths() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QString error;
        QCOMPARE(
            PortalPickerParsing::decodeNullTerminatedPath(encodedPath(temp.path()), &error),
            QDir::cleanPath(temp.path()));
        QVERIFY(error.isEmpty());

        QVERIFY(PortalPickerParsing::decodeNullTerminatedPath(
            QFile::encodeName(temp.path()), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("NUL")));

        QByteArray embedded = QFile::encodeName(temp.path());
        embedded.append('\0');
        embedded.append("tail\0", 5);
        QVERIFY(PortalPickerParsing::decodeNullTerminatedPath(embedded, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("embedded"), Qt::CaseInsensitive));

        QVERIFY(PortalPickerParsing::decodeNullTerminatedPath(
            QByteArrayLiteral("relative/path\0"), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("absolute"), Qt::CaseInsensitive));
    }

    void mapsOpenFileOptionsToLightweightPicker() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("multiple"), true);
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("accept_label"), QStringLiteral("Choose"));

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open a file"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.kind, PortalPickerKind::OpenFile);
        QCOMPARE(request.mode, QStringLiteral("open"));
        QVERIFY(request.multiple);
        QCOMPARE(request.initialDirectory, QDir::cleanPath(temp.path()));
        QCOMPARE(request.title, QStringLiteral("Open a file"));
        QCOMPARE(request.acceptLabel, QStringLiteral("Choose"));

        const QStringList arguments = request.pickerArguments();
        QVERIFY(arguments.contains(QStringLiteral("--picker")));
        QVERIFY(arguments.contains(QStringLiteral("open")));
        QVERIFY(arguments.contains(QStringLiteral("--multiple")));
        QVERIFY(arguments.contains(QDir::cleanPath(temp.path())));
    }

    void mapsDirectoryOpenToFolderPickerWithoutMultiplicity() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("multiple"), true);
        options.insert(QStringLiteral("directory"), true);
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Select folder"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.mode, QStringLiteral("folder"));
        QVERIFY(!request.multiple);
    }

    void enforcesPortalMimeAndGlobFiltersOnReturnedPaths() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString image = temp.filePath("photo.png");
        const QString markdown = temp.filePath("README.md");
        const QString binary = temp.filePath("payload.bin");
        writeFile(image);
        writeFile(markdown);
        writeFile(binary);

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("filters"), QVariantList{
            filterVariant(QStringLiteral("Images"), {{1, QStringLiteral("image/*")}}),
            filterVariant(QStringLiteral("Markdown"), {{0, QStringLiteral("*.md")}}),
        });

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);
        QVERIFY(request.pathMatchesFilters(image));
        QVERIFY(request.pathMatchesFilters(markdown));
        QVERIFY(!request.pathMatchesFilters(binary));

        const QStringList arguments = request.pickerArguments();
        const int mimeIndex = arguments.indexOf(QStringLiteral("--mime"));
        QVERIFY(mimeIndex >= 0);
        QCOMPARE(arguments.value(mimeIndex + 1), QStringLiteral("image/*"));
    }

    void currentFilterNarrowsBackendPostValidation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString image = temp.filePath("photo.png");
        const QString markdown = temp.filePath("README.md");
        writeFile(image);
        writeFile(markdown);

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(
            QStringLiteral("current_filter"),
            filterVariant(QStringLiteral("Markdown"), {{0, QStringLiteral("*.md")}}));

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);
        QVERIFY(request.pathMatchesFilters(markdown));
        QVERIFY(!request.pathMatchesFilters(image));
    }

    void saveFileMapsCurrentFileAndCurrentNameSafely() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString existing = temp.filePath("old.txt");
        writeFile(existing);

        QVariantMap options;
        options.insert(QStringLiteral("current_file"), encodedPath(existing));
        options.insert(QStringLiteral("current_name"), QStringLiteral("new.txt"));

        const PortalPickerRequest request =
            PortalPickerRequest::saveFile(QStringLiteral("Save"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.mode, QStringLiteral("save"));
        QCOMPARE(request.initialDirectory, QDir::cleanPath(temp.path()));
        QCOMPARE(request.suggestedName, QStringLiteral("new.txt"));

        const QStringList arguments = request.pickerArguments();
        const int suggestedIndex = arguments.indexOf(QStringLiteral("--suggest-name"));
        QVERIFY(suggestedIndex >= 0);
        QCOMPARE(arguments.value(suggestedIndex + 1), QStringLiteral("new.txt"));
    }

    void saveFileRejectsUnsafeSuggestedLeaf() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("current_name"), QStringLiteral("../escape.txt"));

        const PortalPickerRequest request =
            PortalPickerRequest::saveFile(QStringLiteral("Save"), options);
        QVERIFY(!request.valid);
        QVERIFY(request.error.contains(QStringLiteral("invalid"), Qt::CaseInsensitive));
    }

    void saveFilesDecodesNamesAndAvoidsCollisionsDeterministically() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        writeFile(temp.filePath("report.txt"));

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("files"), QVariantList{
            encodedLeaf(QStringLiteral("report.txt")),
            encodedLeaf(QStringLiteral("image.png")),
        });

        const PortalPickerRequest request =
            PortalPickerRequest::saveFiles(QStringLiteral("Save files"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.mode, QStringLiteral("folder"));
        QCOMPARE(request.saveFiles, QStringList({QStringLiteral("report.txt"), QStringLiteral("image.png")}));

        const QByteArray pickerOutput =
            QUrl::fromLocalFile(temp.path()).toString(QUrl::FullyEncoded).toUtf8() + '\n';
        const PortalPickerResult result =
            PortalPickerResult::fromPickerStdout(request, pickerOutput);
        QVERIFY2(result.valid, qPrintable(result.error));
        QCOMPARE(result.uris.size(), 2);
        QCOMPARE(
            QUrl(result.uris.at(0)).toLocalFile(),
            temp.filePath("report (1).txt"));
        QCOMPARE(
            QUrl(result.uris.at(1)).toLocalFile(),
            temp.filePath("image.png"));
    }

    void pickerStdoutNormalizesLocalUrisAndRejectsRemoteUris() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString filePath = temp.filePath("hello world.txt");
        writeFile(filePath);

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);

        const QByteArray localOutput =
            QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded).toUtf8() + '\n';
        const PortalPickerResult local =
            PortalPickerResult::fromPickerStdout(request, localOutput);
        QVERIFY2(local.valid, qPrintable(local.error));
        QCOMPARE(local.uris, QStringList({QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded)}));

        const PortalPickerResult remote =
            PortalPickerResult::fromPickerStdout(
                request,
                QByteArrayLiteral("sftp://example.invalid/home/file.txt\n"));
        QVERIFY(!remote.valid);
        QVERIFY(remote.error.contains(QStringLiteral("local"), Qt::CaseInsensitive));
    }

    void savePickerMayReturnAPathThatDoesNotExistYet() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("current_name"), QStringLiteral("future.txt"));
        const PortalPickerRequest request =
            PortalPickerRequest::saveFile(QStringLiteral("Save"), options);
        QVERIFY(request.valid);

        const QString future = temp.filePath("future.txt");
        QVERIFY(!QFileInfo::exists(future));
        const QByteArray output =
            QUrl::fromLocalFile(future).toString(QUrl::FullyEncoded).toUtf8() + '\n';
        const PortalPickerResult result =
            PortalPickerResult::fromPickerStdout(request, output);
        QVERIFY2(result.valid, qPrintable(result.error));
        QCOMPARE(QUrl(result.uris.constFirst()).toLocalFile(), future);
    }
};

QTEST_GUILESS_MAIN(PortalPickerRequestTest)
#include "PortalPickerRequestTest.moc"
