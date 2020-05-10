#include "SerialPortBinaryStream.h"
namespace Platform
{      
        /**
         * Constructor.
         * \param string portName - port name, e.g. /dev/ttyUSB1 or COM3
         * \param uint baudRate - port rate
         * \param uint flowControl - hardware flow control
         * 0 - No flow control
         * 1 - Hardware flow control
         * 2 - Software flow control
         * \param uint minBufferSize - controls minimum buffer swap condition
         * \param uint maxBufferSize - controls maximum buffer swap condition
         * \throws [[[exc]]] if cannot open the
         * serial device
         */
        SerialPortBinaryStream::SerialPortBinaryStream(QString portName, unsigned int baudRate, unsigned int flowControl)
        {
            //Set parameters
            _serial = new QSerialPort();
            _lock = new QMutex(QMutex::Recursive);
            _serial->setPortName(portName);
            _serial->setBaudRate(baudRate);
            bool ok = _serial->open(QIODevice::ReadWrite);
            sLogger.Debug(QString("[SerialPort] Opening port %1 at %2 baud: %3").arg(portName).arg(baudRate).arg(ok));
            if (!ok) throw 2; 
            //_serial->setDataBits(QSerialPort::Data8);
            //_serial->setParity(QSerialPort::NoParity);
            //_serial->setStopBits(QSerialPort::OneStop);
            _serial->setFlowControl((QSerialPort::FlowControl)flowControl);
            _buffer = new QByteArray();
            connect(_serial, &QSerialPort::readyRead, this, [=](){
                sLogger.Debug(QString("[SerialPort] %1 bytes available").arg(_serial->bytesAvailable()));
                _lock->lock();
                _buffer->append(_serial->readAll());
                _lock->unlock();
            });
        }

        void SerialPortBinaryStream::asyncRead()
        {

        }

        void SerialPortBinaryStream::write(QByteArray data)
        {
            sLogger.Debug(QString("[SerialPort] %1 bytes written").arg(writeBytes(data)));
        }

        int SerialPortBinaryStream::writeBytes(QByteArray data)
        {
            int bytes = _serial->write(data);    
            return bytes;
        }

        QByteArray SerialPortBinaryStream::read(qint64 maxlen)
        {
            QByteArray bytes;
            while(_buffer->size()<maxlen){
                _serial->waitForReadyRead();
            }
            _lock->lock();
            bytes = _buffer->left(maxlen);
            _buffer->remove(0,maxlen);
            _lock->unlock();
            return bytes;
        }

        qint64 SerialPortBinaryStream::read(char * data, qint64 maxSize)
        {
            QByteArray bytes = read(maxSize);
            memcpy(data, bytes.data(), bytes.size());
            return bytes.size();
        }

        qint64 SerialPortBinaryStream::peek(char * data, qint64 maxSize)
        {
            QByteArray bytes = peek(maxSize);
            memcpy(data, bytes.data(), bytes.size());
            return bytes.size();
        }

        QByteArray SerialPortBinaryStream::peek(qint64 maxlen)
        {
            QByteArray bytes;
            while(_buffer->size()<maxlen){
                _serial->waitForReadyRead();
            }
            _lock->lock();
            bytes = _buffer->left(maxlen);
            _lock->unlock();
            return bytes;
        }

        void SerialPortBinaryStream::purgeBuffers()
        {
            _serial->flush();
            _serial->clear();
        }

        void SerialPortBinaryStream::flush()
        {
            _serial->flush();
        }

        bool SerialPortBinaryStream::isOpen()
        {
            return _serial->isOpen();
        }

        void SerialPortBinaryStream::close() 
        {
            _serial->close();
        }
}