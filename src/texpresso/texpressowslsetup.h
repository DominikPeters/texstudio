#ifndef TEXPRESSOWSLSETUP_H
#define TEXPRESSOWSLSETUP_H
class QWidget;
class QSettings;
#include <QString>
bool configureTexpressoWsl(QWidget *parent, QSettings *settings, const QString &root);
bool configureTexpressoEnvironment(QWidget *parent, QSettings *settings, const QString &root);
#endif
