#pragma once

#include <vector>
#include <iostream>
#include <QtCore/QtCore>
#include <QtSerialPort/QtSerialPort>
#include "Greis/IBinaryStream.h"
#include "Common/Exception.h"
#include "Common/Logger.h"
namespace Platform
{
    class SerialPortBinaryStream : public QObject, public Greis::IBinaryStream
    {
        Q_OBJECT
    private:
        QSerialPort * _serial;
        QByteArray * _bufferA;
        QByteArray * _bufferB;
        QByteArray * _primaryBuffer; // Write buffer
        QByteArray * _secondaryBuffer; // Read buffer
        unsigned int _maxBufferSize; // Rotate when primary is this long
        unsigned int _minBufferSize; // Rotate when primary is this long
        QMutex mutexSwap;
        void _swapBuffers();
    public:
        SMART_PTR_T(SerialPortBinaryStream);
        SerialPortBinaryStream(QString portName, unsigned int baudRate, unsigned int flowControl, unsigned int minBufferSize, unsigned int maxBufferSize);
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
        