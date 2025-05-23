#include "espocombobox.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>

espoComboBox::espoComboBox(QWidget *parent) : QComboBox(parent)
{

}

void espoComboBox::readWaveformList(void)
{
#if defined(PLATFORM_ANDROID)
    QFile file("assets:/waveforms/_list.wfl");
#else
    QFile file(QStandardPaths::locate(QStandardPaths::AppDataLocation, "waveforms/_list.wfl"));
#endif

    qDebug() << "opening" << file.fileName();
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("could not open %s", qUtf8Printable(file.fileName()));

    QStringList newNames;
    while (!file.atEnd())
        newNames.append(file.readLine().trimmed());
    this->addItems(newNames);
    file.close();
}
