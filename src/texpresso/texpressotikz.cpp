#include "texpressotikz.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#ifdef Q_OS_MACOS
#include <CoreServices/CoreServices.h>
#endif

QList<TexpressoTikz::SourceFigure> TexpressoTikz::sourceFigures(const QString &s)
{
    QList<SourceFigure> result;
    int line = 1, begin = -1, beginLine = 0, depth = 0;
    QList<int> braces;
    // A lexical scan, not a regex: comments, escaped backslashes and verbatim
    // content must not invent figure boundaries. Macro calls deliberately do
    // not resolve to their definitions: the editor needs a literal source root.
    for (int i = 0; i < s.size();) {
        if (s[i] == '\n') { ++line; ++i; continue; }
        if (s[i] == '%') { while (i < s.size() && s[i] != '\n') ++i; continue; }
        if (s[i] == '{') braces.append(i);
        if (s[i] == '}' && !braces.isEmpty()) {
            const int opening = braces.takeLast();
            for (auto &figure : result)
                if (figure.offset > opening && figure.endOffset <= i) figure.enclosingEndLines.append(line);
        }
        if (s[i++] != '\\' || i == s.size()) continue;
        const int start = i - 1, commandLine = line;
        QString command;
        if (s[i].isLetter() || s[i] == '@') {
            while (i < s.size() && (s[i].isLetter() || s[i] == '@')) command += s[i++];
        } else { ++i; continue; }
        if (command == "verb" || command == "lstinline") {
            if (i < s.size() && s[i] == '*') ++i;
            if (i < s.size() && !s[i].isSpace()) {
                const auto delimiter = s[i++];
                while (i < s.size() && s[i] != delimiter && s[i] != '\n') ++i;
                if (i < s.size() && s[i] == delimiter) ++i;
            }
            continue;
        }
        if (command != "begin" && command != "end") continue;
        while (i < s.size() && s[i].isSpace()) { if (s[i++] == '\n') ++line; }
        if (i == s.size() || s[i++] != '{') continue;
        QString environment;
        while (i < s.size() && s[i] != '}' && s[i] != '\n') environment += s[i++];
        if (i == s.size() || s[i++] != '}') continue;
        if (command == "begin" && (environment == "verbatim" || environment == "verbatim*" ||
            environment == "lstlisting" || environment == "minted" || environment == "comment")) {
            const int end = s.indexOf("\\end{" + environment + '}', i);
            const int next = end < 0 ? s.size() : end + environment.size() + 6;
            line += s.mid(i, next - i).count('\n'); i = next; continue;
        }
        if (environment != "tikzpicture" && environment != "tikzpicture*") continue;
        if (command == "begin") {
            if (depth++ == 0) { begin = start; beginLine = commandLine; }
        } else if (depth && --depth == 0) {
            result.append({beginLine, line + 1, begin, i}); begin = -1;
        }
    }
    return result;
}

TexpressoTikz::SourceFigure TexpressoTikz::resolveFigure(const QList<SourceFigure> &figures, int line, int endLine)
{
    QList<SourceFigure> matches;
    // TeX reads an entire macro argument before executing it. A scalebox or
    // rotatebox spanning lines therefore reports its closing-argument line for
    // both picture endpoints. Resolve only a unique literal picture within a
    // balanced source argument ending on precisely that line. Never choose the
    // nearest preceding picture or guess among multiple candidates.
    for (const auto &figure : figures)
        if ((figure.line == line && figure.endLine == endLine) ||
            (endLine == line + 1 && figure.enclosingEndLines.contains(line))) matches.append(figure);
    return matches.size() == 1 ? matches.first() : SourceFigure{};
}

QString TexpressoTikz::findEditor(const QString &overridePath)
{
    const auto usable = [](const QString &path) {
        QFileInfo f(path);
#ifdef Q_OS_MACOS
        // Tauri's executable name can differ from the product or CLI name
        // (released TikZ Editor bundles currently use "app"). Resolve the
        // bundle's declared executable instead of guessing Contents/MacOS.
        if (f.isDir() && f.suffix().compare("app", Qt::CaseInsensitive) == 0) {
            const auto bytes = QFile::encodeName(f.absoluteFilePath());
            const auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
                reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size(), true);
            if (!url) return QString();
            const auto info = CFBundleCopyInfoDictionaryForURL(url);
            CFRelease(url);
            if (!info) return QString();
            const auto value = CFDictionaryGetValue(info, kCFBundleExecutableKey);
            const auto name = value && CFGetTypeID(value) == CFStringGetTypeID()
                ? QString::fromCFString(static_cast<CFStringRef>(value)) : QString();
            CFRelease(info);
            if (name.isEmpty() || name == "." || name == ".." || name.contains('/')) return QString();
            f.setFile(QDir(f.absoluteFilePath()).filePath("Contents/MacOS/" + name));
        }
#endif
        return f.isFile() && f.isExecutable() ? f.absoluteFilePath() : QString();
    };
    if (!overridePath.isEmpty()) return usable(overridePath);
    const auto environment = qEnvironmentVariable("TEXSTUDIO_TIKZ_EDITOR");
    if (!environment.isEmpty()) return usable(environment);
    QStringList candidates;
#ifdef Q_OS_MACOS
    CFURLRef url = nullptr;
    if (LSFindApplicationForInfo(kLSUnknownCreator, CFSTR("com.tikz.editor"), nullptr, nullptr, &url) == noErr && url) {
        const auto path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
        if (path) { candidates << QString::fromCFString(path); CFRelease(path); }
        CFRelease(url);
    }
    candidates << QDir::homePath() + "/Applications/TikZ Editor.app"
               << "/Applications/TikZ Editor.app";
#elif defined(Q_OS_WIN)
    for (const auto &scope : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        QSettings key(QString(scope) + "\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\tikz-editor.exe", QSettings::NativeFormat);
        candidates << key.value(".").toString();
    }
    candidates << qEnvironmentVariable("LOCALAPPDATA") + "/TikZ Editor/tikz-editor.exe"
               << qEnvironmentVariable("ProgramFiles") + "/TikZ Editor/tikz-editor.exe";
#else
    // Installed deb/rpm packages and desktop-integrated AppImages register a
    // launcher even when the executable itself is not on PATH.
    for (const auto &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        for (const auto &name : {"com.tikz.editor.desktop", "tikz-editor.desktop", "TikZ Editor.desktop"}) {
            QSettings desktop(QDir(base).filePath(QString("applications/") + name), QSettings::IniFormat);
            const auto args = QProcess::splitCommand(desktop.value("Desktop Entry/Exec").toString());
            if (args.isEmpty()) continue;
            const auto command = args.first();
            candidates << (QFileInfo(command).isAbsolute() ? command : QStandardPaths::findExecutable(command));
        }
    }
#endif
    candidates << QStandardPaths::findExecutable("tikz-editor");
    for (const auto &path : candidates) { const auto found = usable(path); if (!found.isEmpty()) return found; }
    return {};
}

QIcon TexpressoTikz::editorIcon(const QString &executable)
{
    if (executable.isEmpty()) return {};
    QFileInfo file(executable);
#ifdef Q_OS_MACOS
    const auto bundle = QDir::cleanPath(file.dir().absoluteFilePath("../.."));
    if (QFileInfo(bundle).suffix().compare("app", Qt::CaseInsensitive) == 0) {
        QSettings info(bundle + "/Contents/Info.plist", QSettings::NativeFormat);
        auto name = info.value("CFBundleIconFile").toString();
        if (!name.isEmpty() && QFileInfo(name).fileName() == name) {
            if (QFileInfo(name).suffix().isEmpty()) name += ".icns";
            QIcon icon(bundle + "/Contents/Resources/" + name);
            if (!icon.pixmap(32, 32).isNull()) return icon;
        }
        file.setFile(bundle);
    }
#elif !defined(Q_OS_WIN)
    for (const auto &name : {"com.tikz.editor", "tikz-editor"}) {
        const auto icon = QIcon::fromTheme(name);
        if (!icon.isNull()) return icon;
    }
#endif
    return QFileIconProvider().icon(file);
}

bool TexpressoTikz::launch(const QString &executable, const QString &file, int line)
{
    if (executable.isEmpty() || line < 1 || !QFileInfo(file).isFile()) return false;
    // argv, never a shell command. Launching the executable also forwards the
    // selection to an already-running Tauri single-instance editor on macOS.
    return QProcess::startDetached(executable, {"--source-line=" + QString::number(line), QFileInfo(file).absoluteFilePath()});
}
