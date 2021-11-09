#include "espocombobox.h"

espoComboBox::espoComboBox(QWidget *parent) : QComboBox(parent)
{

}

void espoComboBox::readWaveformList(void)
{
#ifdef PLATFORM_ANDROID
    QString path("assets:");
#else
    QString path = QCoreApplication::applicationDirPath();
#endif
    QFile file(path.append("/waveforms/_list.wfl"));

    qDebug() << "opening" << file.fileName();
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("could not open %s", qUtf8Printable(file.fileName()));

    QStringList newNames;
    while (!file.atEnd())
        newNames.append(file.readLine().trimmed());
    this->addItems(newNames);
    file.close();
}
