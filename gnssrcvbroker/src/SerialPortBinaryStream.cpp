#include "SerialPortBinaryStream.h"
namespace Platform
{
        void SerialPortBinaryStream::_swapBuffers()
        {
            sLogger.Debug("[SerialPort] _swapBuffers called");
            while(true){
                QCoreApplication::processEvents();
                mutexSwap.lock();
                if(_primaryBuffer->size()<_minBufferSize){
                    mutexSwap.unlock();    
                    continue;
                } else {
                    break;
                }
            }
            sLogger.Debug("[SerialPort] Swapping buffers");
            if(_primaryBuffer == _bufferA){
                  sLogger.Debug(QString("[SerialPort] Primary was A (%1 bytes long, B is %2 bytes long").arg(_bufferA->size()).arg(_bufferB->size()));;
                  _primaryBuffer = _bufferB;
                  _secondaryBuffer = _bufferA;
            } else {
                  sLogger.Debug(QString("[SerialPort] Primary was B (%1 bytes long, A is %2 bytes long").arg(_bufferB->size()).arg(_bufferA->size()));;
                  _primaryBuffer = _bufferA;
                  _secondaryBuffer = _bufferB;
            }
            mutexSwap.unlock();            
        }
        
        
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
        SerialPortBinaryStream::SerialPortBinaryStream(QString portName, unsigned int baudRate, unsigned int flowControl, unsigned int minBufferSize, unsigned int maxBufferSize)
        {
            //Set parameters
            _serial = new QSerialPort();
            _serial->setPortName(portName);
            _serial->setBaudRate(baudRate);
            bool ok = _serial->open(QIODevice::ReadWrite);
            sLogger.Debug(QString("[SerialPort] Opening port %1 at %2 baud: %3").arg(portName).arg(baudRate).arg(ok));
            if (!ok) throw 2; 
            //_serial->setDataBits(QSerialPort::Data8);
            //_serial->setParity(QSerialPort::NoParity);
            //_serial->setStopBits(QSerialPort::OneStop);
            _serial->setFlowControl((QSerialPort::FlowControl)flowControl);
            _bufferA = new QByteArray();
            _bufferB = new QByteArray();
            _primaryBuffer = _bufferA;
            _secondaryBuffer = _bufferB;
            _maxBufferSize = maxBufferSize;
            _minBufferSize = minBufferSize;
            connect(_serial, &QSerialPort::readyRead, this, [=](){
                sLogger.Debug("[SerialPort] Bytes available");
                mutexSwap.lock();
                sLogger.Debug(QString("[SerialPort] %1 bytes available").arg(_serial->bytesAvailable()));
                _primaryBuffer->append(_serial->readAll());
                mutexSwap.unlock();
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
            mutexSwap.lock();
            int bytes = _serial->write(data);
            mutexSwap.unlock();            
            return bytes;
        }

        QByteArray SerialPortBinaryStream::read(qint64 maxlen)
        {
            QByteArray bytes;
            unsigned int _primaryBufferSize;
            unsigned int _secondaryBufferSize;
            sLogger.Debug(QString("[SerialPort] Serving %1 bytes from buffer").arg(maxlen));
            mutexSwap.lock();
            _primaryBufferSize = _primaryBuffer->size();
            sLogger.Debug(QString("[SerialPort] Primary buffer size is %1").arg(_primaryBufferSize));

            if(_primaryBuffer == _bufferA){
                _secondaryBuffer = _bufferB;
                sLogger.Debug(QString("[SerialPort] Primary is A"));
            }
            if(_primaryBuffer == _bufferB){
                _secondaryBuffer = _bufferA;
                sLogger.Debug(QString("[SerialPort] Primary is B"));
            }

            mutexSwap.unlock();
            _secondaryBufferSize = _secondaryBuffer->size();
            sLogger.Debug(QString("[SerialPort] Secondary buffer size is %1").arg(_secondaryBuffer->size()));

            if(_secondaryBufferSize >= maxlen){
                sLogger.Debug(QString("[SerialPort] Secondary has enough bytes"));
                // There is enough bytes in current buffer
                bytes = _secondaryBuffer->left(maxlen);
                _secondaryBuffer->remove(0,maxlen);
            } else {
                sLogger.Debug(QString("[SerialPort] Secondary lacks enough bytes"));
                // We lack bytes, get all wait for buffer swap
                bytes = QByteArray(*_secondaryBuffer);
                _secondaryBuffer->clear();
                sLogger.Debug(QString("[SerialPort] Consumed %1 bytes from Secondary").arg(bytes.size()));
                while(bytes.size()<maxlen){
                        _swapBuffers();
                        int leftBytes=maxlen-bytes.size();
                        sLogger.Debug(QString("[SerialPort] Consumed %1 bytes from Secondary").arg(leftBytes));
                        bytes.append(_secondaryBuffer->left(leftBytes));
                        _secondaryBuffer->remove(0,leftBytes);
                    }
            }
            

            if(_primaryBufferSize >= _maxBufferSize){
              _swapBuffers();
            }

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
            unsigned int _secondaryBufferSize = _secondaryBuffer->size();
            bytes = _secondaryBuffer->left(maxlen);
            if(_secondaryBufferSize >= maxlen){
                return bytes;     
            } else {
                mutexSwap.lock();
                while((_primaryBuffer->size()) < (maxlen-bytes.size())){
                   mutexSwap.unlock();
                   QCoreApplication::processEvents();
                   mutexSwap.lock();
                }  
                bytes.append(_primaryBuffer->left(maxlen-bytes.size()));
                mutexSwap.unlock();
            }
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