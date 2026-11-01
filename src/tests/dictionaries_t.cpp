#include "texstudio_t.h"
#include "texstudio.h"
#include "dictionaryarchive.h"
#include "spellerutility.h"
#include "thesaurusdialog.h"
#include "utilsSystem.h"
#include "quazip.h"
#include "quazipfile.h"
#include <QtTest>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtConcurrent>

extern Texstudio *txsInstance;

namespace {
void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QByteArray read(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void zip(const QString &path, const QMap<QString, QByteArray> &files)
{
    QuaZip archive(path); QVERIFY(archive.open(QuaZip::mdCreate));
    for (auto it = files.begin(); it != files.end(); ++it) {
        QuaZipFile output(&archive);
        QVERIFY(output.open(QIODevice::WriteOnly, QuaZipNewInfo(it.key())));
        QCOMPARE(output.write(it.value()), it.value().size());
        output.close(); QCOMPARE(output.getZipError(), ZIP_OK);
    }
    archive.close(); QCOMPARE(archive.getZipError(), ZIP_OK);
}
}

void TexStudioTest::compressedDictionaries()
{
    QTemporaryDir temp; QVERIFY(temp.isValid());
    const auto oldThesaurus = txsInstance->configManager.parseDir(txsInstance->configManager.thesaurus_database);
    const auto originalCache = qgetenv("TEXSTUDIO_TEST_DICTIONARY_CACHE");
    qputenv("TEXSTUDIO_TEST_DICTIONARY_CACHE", temp.filePath("cache").toUtf8());
    const auto restore = qScopeGuard([&] {
        ThesaurusDialog::prepareDatabase({});
        if (originalCache.isNull()) qunsetenv("TEXSTUDIO_TEST_DICTIONARY_CACHE");
        else qputenv("TEXSTUDIO_TEST_DICTIONARY_CACHE", originalCache);
        ThesaurusDialog::setUserPath(txsInstance->configManager.configFileNameBase);
        ThesaurusDialog::prepareDatabase(oldThesaurus);
    });
    const auto resources = QFileInfo(temp.path()).canonicalFilePath() + "/resources", user = temp.filePath("user");
    QVERIFY(QDir().mkpath(resources)); QVERIFY(QDir().mkpath(user));
    const auto dictionary = resources + "/example.dic";
    const QByteArray affix("SET UTF-8\n"), words("3\nhello\nworld\neditor\n");
    zip(dictionary + ".zip", {{"example.dic", words}, {"example.aff", affix}});
    zip(resources + "/unused.dic.zip", {{"unused.dic", words}, {"unused.aff", affix}});
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(findResourceFile("example.dic", true, {resources}), dictionary);
    QVERIFY(DictionaryArchive::exists(dictionary));
    QVERIFY(DictionaryArchive::exists(dictionary + ".zip"));
    SpellerManager manager;
    manager.setIgnoreFilePrefix(user + '/'); manager.setDictPaths({resources});
    QVERIFY(manager.availableDicts().contains("example"));
    QVERIFY(manager.availableDicts().contains("unused"));
    QVERIFY(!QFileInfo::exists(temp.filePath("cache"))); // Listing never extracts.
    auto speller = manager.getSpeller("example"); QVERIFY(speller);
    QVERIFY(speller->check("hello")); QVERIFY(!speller->check("helllo"));
    QVERIFY(speller->suggest("helllo").contains("hello"));
    QCOMPARE(speller->getCurrentDic(), dictionary);
    speller->addToIgnoreList("mycustomword"); manager.unloadAll();
    QVERIFY(read(user + "/example.ign").contains("mycustomword"));
    QVERIFY(!QFileInfo::exists(resources + "/example.ign"));
    QString error;
    const auto extracted = DictionaryArchive::materialize(dictionary, &error);
    QVERIFY2(!extracted.isEmpty(), qPrintable(error)); QCOMPARE(read(extracted), words);
    const auto cache = QFileInfo(extracted).absolutePath();
    QCOMPARE(QDir(temp.filePath("cache/dictionaries/v1")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
    const auto modified = QFileInfo(extracted).lastModified();
    QCOMPARE(DictionaryArchive::materialize(dictionary), extracted);
    QCOMPARE(QFileInfo(extracted).lastModified(), modified);
    // Same-size damage, missing files and complete OS cache removal all repair.
    write(extracted, QByteArray(words.size(), 'x'));
    QCOMPARE(DictionaryArchive::materialize(dictionary), extracted); QCOMPARE(read(extracted), words);
    QVERIFY(QFile::remove(cache + "/example.aff"));
    QCOMPARE(DictionaryArchive::materialize(dictionary), extracted); QCOMPARE(read(cache + "/example.aff"), affix);
    QVERIFY(QDir(cache).removeRecursively());
    auto a = QtConcurrent::run([&] { return DictionaryArchive::materialize(dictionary); });
    auto b = QtConcurrent::run([&] { return DictionaryArchive::materialize(dictionary); });
    QCOMPARE(a.result(), extracted); QCOMPARE(b.result(), extracted);
    speller = manager.getSpeller("example"); QVERIFY(speller); QVERIFY(speller->check("mycustomword"));
    manager.unloadAll();
    zip(dictionary + ".zip", {{"example.dic", "1\nchanged\n"}, {"example.aff", affix}});
    const auto updated = DictionaryArchive::materialize(dictionary, &error);
    QVERIFY2(!updated.isEmpty(), qPrintable(error)); QVERIFY(updated != extracted);
    speller = manager.getSpeller("example"); QVERIFY(speller); QVERIFY(speller->check("changed"));
    QVERIFY(speller->check("mycustomword"));
    // Ordinary user dictionaries still load directly and override adjacent ZIPs.
    write(dictionary, words); write(resources + "/example.aff", affix);
    QCOMPARE(DictionaryArchive::materialize(dictionary), dictionary);
    QVERIFY(!DictionaryArchive::isCompressed(dictionary));
    const auto thesaurus = resources + "/th_example_v2.dat";
    const QByteArray data("UTF-8\nhappy|1\n(adjective)|joyful|glad\n");
    zip(thesaurus + ".zip", {{"th_example_v2.dat", data}});
    QCOMPARE(read(DictionaryArchive::materialize(thesaurus + ".zip", &error)), data);
    write(user + "/th_example_v2.my", "Mood|happy|radiant\n");
    ThesaurusDialog::setUserPath(user + '/'); ThesaurusDialog::prepareDatabase(thesaurus);
    QVERIFY(ThesaurusDialog::retrieveDatabase());
    {
        ThesaurusDialog dialog; dialog.setSearchWord("happy");
        bool found = false, custom = false;
        for (auto list : dialog.findChildren<QListWidget *>()) {
            found |= !list->findItems("joyful", Qt::MatchExactly).isEmpty();
            custom |= !list->findItems("radiant", Qt::MatchExactly).isEmpty();
        }
        QVERIFY(found); QVERIFY(custom);
    }
    QVERIFY(read(user + "/th_example_v2.my").contains("radiant"));
    QVERIFY(!QFileInfo::exists(resources + "/th_example_v2.my"));
    const auto invalid = resources + "/bad.dic";
    zip(invalid + ".zip", {{"bad.dic", words}, {"../escape.aff", affix}});
    QVERIFY(DictionaryArchive::materialize(invalid, &error).isEmpty()); QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(temp.filePath("escape.aff")));
    zip(invalid + ".zip", {{"bad.dic", words}});
    QVERIFY(DictionaryArchive::materialize(invalid, &error).isEmpty()); QVERIFY(!error.isEmpty());
    write(invalid + ".zip", "not a zip");
    QVERIFY(DictionaryArchive::materialize(invalid, &error).isEmpty()); QVERIFY(!error.isEmpty());
    zip(invalid + ".zip", {{"bad.dic", words}, {"bad.aff", affix}});
    auto damaged = read(invalid + ".zip");
    const int central = damaged.indexOf(QByteArray("PK\x01\x02", 4)); QVERIFY(central >= 0);
    damaged[central + 16] = char(damaged.at(central + 16) ^ 1); // Central-directory CRC.
    write(invalid + ".zip", damaged);
    QVERIFY(DictionaryArchive::materialize(invalid, &error).isEmpty()); QVERIFY(!error.isEmpty());
    // Exercise the actual packaged languages, not just generated fixtures.
    const auto bundledDictionary = findResourceFile("en_US.dic", true);
    const auto bundled = QFileInfo(bundledDictionary).absolutePath();
    if (DictionaryArchive::isCompressed(bundledDictionary)) {
        // The runner's disposable cache survives two test processes so it can
        // verify reuse across launches. All synthetic fixtures use temp/cache.
        if (!originalCache.isEmpty()) qputenv("TEXSTUDIO_TEST_DICTIONARY_CACHE", originalCache);
        SpellerManager real; real.setIgnoreFilePrefix(user + '/'); real.setDictPaths({bundled});
        QVERIFY(real.availableDicts().size() > 80);
        auto english = real.getSpeller("en_US"); QVERIFY(english); QVERIFY(english->check("dictionary"));
        auto german = real.getSpeller("de_DE"); QVERIFY(german); QVERIFY(german->check("Wörterbuch"));
        QVERIFY(!QFileInfo::exists(bundled + "/en_US.dic"));
    }
}
