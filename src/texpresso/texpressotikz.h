#ifndef TEXPRESSOTIKZ_H
#define TEXPRESSOTIKZ_H
#include <QString>
#include <QList>
#include <QIcon>

namespace TexpressoTikz {
struct SourceFigure {
    int line = 0, endLine = 0; // one-based; end exclusive
    int offset = 0, endOffset = 0; // UTF-16; end exclusive
    QList<int> enclosingEndLines; // closing braces of enclosing source arguments
};
QList<SourceFigure> sourceFigures(const QString &source);
SourceFigure resolveFigure(const QList<SourceFigure> &figures, int inputLine, int endInputLine);
QString findEditor(const QString &overridePath = QString());
QIcon editorIcon(const QString &executable);
bool launch(const QString &executable, const QString &file, int oneBasedLine);
}
#endif
