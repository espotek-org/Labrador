#ifndef PINCHCATCHER_H
#define PINCHCATCHER_H

#include <QObject>

class pinchCatcher : public QObject
{
    Q_OBJECT
public:
    explicit pinchCatcher(QObject *parent = 0);

signals:

public slots:
};

#endif // PINCHCATCHER_H