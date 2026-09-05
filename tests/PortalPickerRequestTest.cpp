// SPDX-License-Identifier: GPL-3.0-only

#include "picker/PickerController.hpp"
#include "portal/PortalPickerRequest.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

    static QVariant choiceVariant(
        const QString& id,
        const QString& label,
        const QList<PortalChoiceOption>& options,
        const QString& initial) {
        QVariantList serializedOptions;
        for (const PortalChoiceOption& option : options) {
            serializedOptions.push_back(QVariantMap{
                {QStringLiteral("id"), option.id},
                {QStringLiteral("label"), option.label},
            });
        }
        return QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("options"), serializedOptions},
            {QStringLiteral("initial"), initial},
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

        const QStringList processArguments = request.pickerProcessArguments();
        QVERIFY(processArguments.contains(QStringLiteral("--picker-title=Open a file")));
        QVERIFY(processArguments.contains(QStringLiteral("--accept-label=Choose")));
        QVERIFY(processArguments.contains(QStringLiteral("--portal-context-stdin")));
        QVERIFY(!processArguments.contains(QStringLiteral("--picker-title")));
        QVERIFY(!processArguments.contains(QStringLiteral("--accept-label")));
    }

    void presentationMetadataCannotBecomePickerOptions() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("accept_label"), QStringLiteral("--version"));

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("--help"), options);
        QVERIFY(request.valid);

        const QStringList arguments = request.pickerProcessArguments();
        QVERIFY(arguments.contains(QStringLiteral("--picker-title=--help")));
        QVERIFY(arguments.contains(QStringLiteral("--accept-label=--version")));
        QVERIFY(!arguments.contains(QStringLiteral("--help")));
        QVERIFY(!arguments.contains(QStringLiteral("--version")));
    }

    void mapsDirectoryOpenToMultipleFolderPicker() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("multiple"), true);
        options.insert(QStringLiteral("directory"), true);
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Select folders"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.mode, QStringLiteral("folder"));
        QVERIFY(request.multiple);
        QVERIFY(request.pickerArguments().contains(QStringLiteral("--multiple")));
    }

    void multipleFolderPickerContractAcceptsOnlySelectedDirectories() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath(QStringLiteral("first"));
        const QString second = temp.filePath(QStringLiteral("second"));
        const QString file = temp.filePath(QStringLiteral("not-a-folder.txt"));
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        writeFile(file);

        const PickerContract contract = PickerContract::parse(
            QStringLiteral("folder"),
            true,
            temp.path(),
            {});
        QVERIFY2(contract.valid, qPrintable(contract.error));
        QVERIFY(contract.folderMode);
        QVERIFY(contract.multiple);
        QVERIFY(contract.canAccept({first, second}, temp.path()));
        QCOMPARE(
            contract.acceptedPaths({first, second}, temp.path()),
            QStringList({QDir::cleanPath(first), QDir::cleanPath(second)}));
        QVERIFY(!contract.canAccept({}, temp.path()));
        QVERIFY(!contract.canAccept({first, file}, temp.path()));

        const PickerContract single = PickerContract::parse(
            QStringLiteral("folder"),
            false,
            temp.path(),
            {});
        QVERIFY(single.valid);
        QCOMPARE(
            single.acceptedPaths({}, temp.path()),
            QStringList({QDir::cleanPath(temp.path())}));

        const PickerContract saveMultiple = PickerContract::parse(
            QStringLiteral("save"),
            true,
            temp.path(),
            {});
        QVERIFY(!saveMultiple.valid);
    }

    void multipleFolderPickerResultValidatesEveryDirectory() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath(QStringLiteral("folder-a"));
        const QString second = temp.filePath(QStringLiteral("folder-b"));
        const QString file = temp.filePath(QStringLiteral("file.txt"));
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        writeFile(file);

        QVariantMap options;
        options.insert(QStringLiteral("multiple"), true);
        options.insert(QStringLiteral("directory"), true);
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Select folders"), options);
        QVERIFY(request.valid);
        QVERIFY(request.multiple);

        const QJsonObject good{
            {QStringLiteral("version"), 1},
            {QStringLiteral("uris"), QJsonArray{
                QUrl::fromLocalFile(first).toString(QUrl::FullyEncoded),
                QUrl::fromLocalFile(second).toString(QUrl::FullyEncoded),
            }},
            {QStringLiteral("filter"), -1},
            {QStringLiteral("choices"), QJsonObject{}},
        };
        PortalPickerResult result = PortalPickerResult::fromPickerStdout(
            request,
            QJsonDocument(good).toJson(QJsonDocument::Compact));
        QVERIFY2(result.valid, qPrintable(result.error));
        QCOMPARE(result.uris.size(), 2);

        QJsonObject bad = good;
        bad.insert(
            QStringLiteral("uris"),
            QJsonArray{
                QUrl::fromLocalFile(first).toString(QUrl::FullyEncoded),
                QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded),
            });
        result = PortalPickerResult::fromPickerStdout(
            request,
            QJsonDocument(bad).toJson(QJsonDocument::Compact));
        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("non-directory"), Qt::CaseInsensitive));
    }

    void preservesFilterListAndCurrentSelection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QVariant images =
            filterVariant(QStringLiteral("Images"), {{1, QStringLiteral("image/*")}});
        const QVariant markdown =
            filterVariant(QStringLiteral("Markdown"), {{0, QStringLiteral("*.md")}});

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("filters"), QVariantList{images, markdown});
        options.insert(QStringLiteral("current_filter"), markdown);

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.filters.size(), 2);
        QCOMPARE(request.filters.at(0).name, QStringLiteral("Images"));
        QCOMPARE(request.filters.at(1).name, QStringLiteral("Markdown"));
        QCOMPARE(request.initialFilterIndex, 1);
        QVERIFY(!request.filterLocked);

        const QStringList arguments = request.pickerArguments();
        QVERIFY(!arguments.contains(QStringLiteral("--mime")));
    }

    void currentFilterWithoutFilterListRemainsUsableAndLocked() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(
            QStringLiteral("current_filter"),
            filterVariant(QStringLiteral("Markdown"), {{0, QStringLiteral("*.md")}}));

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);
        QCOMPARE(request.filters.size(), 1);
        QCOMPARE(request.filters.constFirst().name, QStringLiteral("Markdown"));
        QCOMPARE(request.initialFilterIndex, 0);
        QVERIFY(request.filterLocked);
    }

    void portalFiltersGuideButDoNotRejectReturnedSelection() {
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

        const QByteArray output =
            QUrl::fromLocalFile(binary).toString(QUrl::FullyEncoded).toUtf8() + '\n';
        const PortalPickerResult result =
            PortalPickerResult::fromPickerStdout(request, output);
        QVERIFY2(result.valid, qPrintable(result.error));
        QCOMPARE(
            result.uris,
            QStringList({QUrl::fromLocalFile(binary).toString(QUrl::FullyEncoded)}));
        QCOMPARE(result.selectedFilterIndex, request.initialFilterIndex);
    }

    void decodesPortalChoicesAndSerializesPickerContext() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("filters"), QVariantList{
            filterVariant(QStringLiteral("Markdown"), {{0, QStringLiteral("*.md")}}),
            filterVariant(QStringLiteral("Text"), {{0, QStringLiteral("*.txt")}}),
        });
        options.insert(QStringLiteral("choices"), QVariantList{
            choiceVariant(
                QStringLiteral("readonly"),
                QStringLiteral("Open read-only"),
                {},
                QStringLiteral("true")),
            choiceVariant(
                QStringLiteral("encoding"),
                QStringLiteral("Encoding"),
                {
                    {QStringLiteral("utf8"), QStringLiteral("UTF-8")},
                    {QStringLiteral("latin1"), QStringLiteral("Latin-1")},
                },
                QStringLiteral("latin1")),
        });

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY2(request.valid, qPrintable(request.error));
        QCOMPARE(request.choices.size(), 2);
        QVERIFY(request.choices.at(0).boolean);
        QCOMPARE(request.choices.at(0).initialSelection, QStringLiteral("true"));
        QVERIFY(!request.choices.at(1).boolean);
        QCOMPARE(request.choices.at(1).initialSelection, QStringLiteral("latin1"));

        const QJsonObject context = request.pickerContextJson();
        QCOMPARE(context.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(context.value(QStringLiteral("initial_filter")).toInt(), 0);
        QVERIFY(!context.value(QStringLiteral("filter_locked")).toBool());
        QCOMPARE(context.value(QStringLiteral("filters")).toArray().size(), 2);
        QCOMPARE(context.value(QStringLiteral("choices")).toArray().size(), 2);

        const QJsonObject firstFilter =
            context.value(QStringLiteral("filters")).toArray().at(0).toObject();
        QCOMPARE(firstFilter.value(QStringLiteral("name")).toString(), QStringLiteral("Markdown"));
        QCOMPARE(
            firstFilter.value(QStringLiteral("patterns")).toArray().at(0).toString(),
            QStringLiteral("*.md"));
    }

    void structuredPickerResultEchoesFilterAndChoiceSelections() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString selectedFile = temp.filePath("payload.bin");
        writeFile(selectedFile);

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("filters"), QVariantList{
            filterVariant(QStringLiteral("Binary"), {{0, QStringLiteral("*.bin")}}),
            filterVariant(QStringLiteral("Text"), {{0, QStringLiteral("*.txt")}}),
        });
        options.insert(QStringLiteral("choices"), QVariantList{
            choiceVariant(
                QStringLiteral("readonly"),
                QStringLiteral("Open read-only"),
                {},
                QStringLiteral("false")),
            choiceVariant(
                QStringLiteral("mode"),
                QStringLiteral("Mode"),
                {
                    {QStringLiteral("a"), QStringLiteral("Mode A")},
                    {QStringLiteral("b"), QStringLiteral("Mode B")},
                },
                QStringLiteral("a")),
        });

        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);

        const QString uri = QUrl::fromLocalFile(selectedFile).toString(QUrl::FullyEncoded);
        const QJsonObject structured{
            {QStringLiteral("version"), 1},
            {QStringLiteral("uris"), QJsonArray{uri}},
            {QStringLiteral("filter"), 1},
            {QStringLiteral("choices"), QJsonObject{
                {QStringLiteral("readonly"), QStringLiteral("true")},
                {QStringLiteral("mode"), QStringLiteral("b")},
            }},
        };

        const PortalPickerResult result = PortalPickerResult::fromPickerStdout(
            request,
            QJsonDocument(structured).toJson(QJsonDocument::Compact));
        QVERIFY2(result.valid, qPrintable(result.error));
        QCOMPARE(result.uris, QStringList({uri}));
        QCOMPARE(result.selectedFilterIndex, 1);
        QCOMPARE(result.choiceSelections.value(QStringLiteral("readonly")), QStringLiteral("true"));
        QCOMPARE(result.choiceSelections.value(QStringLiteral("mode")), QStringLiteral("b"));
    }

    void structuredPickerResultRejectsUnknownOrInvalidChoiceSelections() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString selectedFile = temp.filePath("payload.bin");
        writeFile(selectedFile);

        QVariantMap options;
        options.insert(QStringLiteral("current_folder"), encodedPath(temp.path()));
        options.insert(QStringLiteral("choices"), QVariantList{
            choiceVariant(
                QStringLiteral("mode"),
                QStringLiteral("Mode"),
                {
                    {QStringLiteral("a"), QStringLiteral("Mode A")},
                    {QStringLiteral("b"), QStringLiteral("Mode B")},
                },
                QStringLiteral("a")),
        });
        const PortalPickerRequest request =
            PortalPickerRequest::openFile(QStringLiteral("Open"), options);
        QVERIFY(request.valid);

        const QString uri = QUrl::fromLocalFile(selectedFile).toString(QUrl::FullyEncoded);
        QJsonObject choices{
            {QStringLiteral("mode"), QStringLiteral("missing")},
        };
        QJsonObject structured{
            {QStringLiteral("version"), 1},
            {QStringLiteral("uris"), QJsonArray{uri}},
            {QStringLiteral("filter"), -1},
            {QStringLiteral("choices"), choices},
        };
        PortalPickerResult result = PortalPickerResult::fromPickerStdout(
            request,
            QJsonDocument(structured).toJson(QJsonDocument::Compact));
        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("choice"), Qt::CaseInsensitive));

        choices.insert(QStringLiteral("mode"), QStringLiteral("a"));
        choices.insert(QStringLiteral("unexpected"), QStringLiteral("value"));
        structured.insert(QStringLiteral("choices"), choices);
        result = PortalPickerResult::fromPickerStdout(
            request,
            QJsonDocument(structured).toJson(QJsonDocument::Compact));
        QVERIFY(!result.valid);
        QVERIFY(result.error.contains(QStringLiteral("unknown"), Qt::CaseInsensitive));
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
        QCOMPARE(request.saveFileNames, QStringList({QStringLiteral("report.txt"), QStringLiteral("image.png")}));

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
