#ifndef Header_Latex_Log_Widget
#define Header_Latex_Log_Widget

#include "logeditor.h"
#include "latexlog.h"

#include <QSortFilterProxyModel>

#include "mostQtHeaders.h"

class LatexLogWidget : public QWidget
{
	Q_OBJECT

public:
	explicit LatexLogWidget(QWidget *parent = 0);

	bool loadLogFile(const QString &logname, const QString &compiledFileName, QTextCodec* fallbackCodec);
	LatexLogModel *getLogModel() { return logModel; }
    // Build decisions always use this model, independent of the visible source.
    LatexLogModel *getBuildLogModel() { return buildModel; }
    enum OutputSource { BuildOutput, LiveOutput, EngineTrace };
    void setOutputRoot(const QString &root);
    QString outputRoot() const { return currentRoot; }
    OutputSource outputSource() const;
    void setOutputSource(OutputSource source);
    void setLiveLog(const QString &root, const QString &text, qint64 sequence, bool current);
    void setLiveState(const QString &root, const QString &status, const QString &trace, qint64 sequence);
    bool currentBufferLog() const { return outputSource() == LiveOutput; }
    bool liveLogCurrent() const;
    void resetBuildLog(const QString &root = QString());
    void setBuildLog(const QString &root, const QString &text);
    QString buildLogRoot() const { return lastBuildRoot; }
    QString logText() const { return log->toPlainText(); }
    static QString outputIdentity(const QString &root);

	bool logEntryNumberValid(int logEntryNumber);
	bool logPresent();
	void resetLog();
	void selectLogEntry(int logEntryNumber);

	void copy();

	bool childHasFocus() const { return log->hasFocus() || errorTable->hasFocus(); }
	QList<QAction *> displayActions();

signals:
	void logEntryActivated(int);
	void logLoaded();
	void logResetted();
    void outputSourceChanged();

public slots:

private slots:
	void clickedOnLogModelIndex(const QModelIndex &index);
	void gotoLogEntry(int logEntryNumber);
	void gotoLogLine(int logLine);
	void copyRow();
	void copyAllRows();
	void copyAllMessagesWithLineNumbers();
	void setWidgetVisibleFromAction(bool visible);
	void setInfo(const QString &message);
	void filterChanged(bool);
	void resizeRows();

private:
	LatexLogModel *logModel;
    LatexLogModel *buildModel;
    struct OutputSnapshot {
        QString buildText, liveText, status, trace;
        bool buildPresent = false, livePresent = false, liveCurrent = false;
        qint64 liveSequence = -1;
        OutputSource source = BuildOutput;
    };
    QHash<QString, OutputSnapshot> outputs;
    QString currentRoot, lastBuildRoot;
    QComboBox *sourceSelector;
    void displayOutput();
	QSortFilterProxyModel *proxyModel;
	bool logpresent;

	QTableView *errorTable;
	LogEditor *log;
	QLabel *infoLabel;
	QAction *displayTableAction, *displayLogAction, *filterErrorAction, *filterWarningAction, *filterBadBoxAction;

	QString m_lastIgnoredFilename; // remember logname which the user did not load to avoid asking again
	bool m_answer; // remember dialog answer

	void copyRowsWithColumnRange(int firstRow, int rows, int first, int last);
	void onInfoLinkActivated(const QString &link);
};

#endif // LATEXLOGWIDGET_H
