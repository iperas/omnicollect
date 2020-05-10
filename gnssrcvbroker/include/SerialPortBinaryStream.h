#pragma once

#include <vector>
#include <iostream>
#include <QtCore/QtCore>
#include <QtSerialPort/QtSerialPort>
#include "greis/IBinaryStream.h"
#include "common/Exception.h"
#include "common/Logger.h"
namespace Platform
{
    class SerialPortBinaryStream : public QObject, public Greis::IBinaryStream
    {
        Q_OBJECT
    private:
        QSerialPort * _serial;
        QByteArray * _buffer;
        QMutex * _lock;
    public:
        SMART_PTR_T(SerialPortBinaryStream);
        SerialPortBinaryStream(QString portName, unsigned int baudRate, unsigned int flowControl);
        void asyncRead();
        void write(QByteArray data);
        int writeBytes(QByteArray data);
        QByteArray read(qint64 maxlen);
        qint64 read(char * data, qint64 maxSize);
        qint64 peek(char * data, qint64 maxSize);
        QByteArray peek(qint64 maxlen);
        void purgeBuffers();
        void flush();
        bool isOpen();
        void close();
    };
}
        