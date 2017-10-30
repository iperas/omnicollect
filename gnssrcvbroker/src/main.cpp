#include <QtCore/QtCore>
#include <QtCore/QReadWriteLock>
#include <QtCore/QDir>
#include "Common/Logger.h"
#include "Common/SmartPtr.h"
#include "Common/Path.h"
#include "Common/Connection.h"
#include "Greis/DataChunk.h"
#include "SerialPortBinaryStream.h"
#include "SerialStreamReader.h"
#include "ChainedSink.h"
#include "Greis/LoggingBinaryStream.h"
#include "Greis/FileBinaryStream.h"
#include "webapi.h"
#include "Greis/SkyPeek.h"
#include "Greis/StdMessage/FileIdStdMessage.h"
#include "Greis/StdMessage/MsgFmtStdMessage.h"
#include "Greis/StdMessage/ParamsStdMessage.h"
#include "Greis/StdMessage/SatIndexStdMessage.h"
#include "Greis/StdMessage/SatNumbersStdMessage.h"
#include "Greis/StdMessage/RcvDateStdMessage.h"
#include "Greis/ChecksumComputer.h"

using namespace Common;
using namespace Greis;
using namespace Platform;

namespace gnssrcvbroker
{

    QReadWriteLock skyPeekLock;
    std::unique_ptr<SkyPeek> skyPeek;

	bool mainLoop(QHash<QString,QVariant> C)
    {
        sLogger.Trace("State: Preparing for acquisition");
        if (!C["enableFile"].toBool() && !C["enableDB"].toBool() && !C["enableRB"].toBool()) {
            sLogger.Warn("No storage options has been set.");
        }

        try
        {
            // Obtaining settings

            QStringList commands;
            auto keys = sIniSettings.settings()->allKeys();
            for (auto key : keys)
            {
                if (key.startsWith("Receiver/command"))
                {
                    commands.append(sIniSettings.value(key).toString());
                }
            }

            QString serialPortName = sIniSettings.value("Receiver/serialPortName", QString()).toString();
            unsigned int serialPortBaudRate = (unsigned int)sIniSettings.value("Receiver/serialPortBaudRate", 115200).toInt();
            unsigned int serialPortFlowControl = (unsigned int)sIniSettings.value("Receiver/serialPortFlowControl", 0).toInt();
            unsigned int minBufferSize = (unsigned int)sIniSettings.value("Receiver/minBufferSize", 1000).toInt();
            unsigned int maxBufferSize = (unsigned int)sIniSettings.value("Receiver/maxBufferSize", 23000).toInt();

            sLogger.Debug(QString("serialportName=%1").arg(serialPortName));
            sLogger.Debug(QString("serialPortBaudRate=%1").arg(serialPortBaudRate));
            sLogger.Debug(QString("serialPortFlowControl=%1").arg(serialPortFlowControl));
            sLogger.Debug(QString("minBufferSize=%1").arg(minBufferSize));
            sLogger.Debug(QString("maxBufferSize=%1").arg(maxBufferSize));

            Platform::SerialPortBinaryStream::SharedPtr_t deviceBinaryStream;

            bool success = false;

            sLogger.Trace("Progress: Opening port");

            deviceBinaryStream = std::make_shared<Platform::SerialPortBinaryStream>(
                serialPortName, serialPortBaudRate, serialPortFlowControl, 
                minBufferSize, maxBufferSize);     

            sLogger.Trace("State: Stopping current monitoring (if any)...");
            deviceBinaryStream->write("\n\rdm\n\r");
            sLogger.Trace("State: Purging buffers...");
            deviceBinaryStream->purgeBuffers();
            
            GreisMessageStream::SharedPtr_t messageStream;

            QDir fileTree;
            bool fileTreeMode = false;

            if (!C["enableFile"].toBool())
            {
                messageStream = std::make_shared<GreisMessageStream>(deviceBinaryStream, true, false);
            } else if ((C["greisLogFileName"].toString()).endsWith(QDir::separator()))
            {
                fileTree = QDir(C["greisLogFileName"].toString());
                QFile file(fileTree.filePath("writetest"));
                if (!file.open(QIODevice::ReadWrite))
                {
                    sLogger.Error(QString("Directory %1 is not writable. File storage disabled.").arg(fileTree.path()));
                    C["enableFile"] = false;
                } else {
                    file.remove();
                    fileTreeMode = true;
                    sLogger.Debug("fileTreeMode=true");
                }
                messageStream = std::make_shared<GreisMessageStream>(deviceBinaryStream, true, false);
            }
            else
            {
                auto now = QDateTime::currentDateTimeUtc();
                QString fileName = QString("%1Z.%2").arg(now.toString("yyyy-MM-dd_HH_mm_ss_zzz")).arg(C["greisLogFileName"].toString());
                auto proxyStream = std::make_shared<LoggingBinaryStream>(deviceBinaryStream, std::make_shared<FileBinaryStream>(fileName, Create));
                messageStream = std::make_shared<GreisMessageStream>(proxyStream, true, false);
                sLogger.Debug("fileTreeMode=false");
            }

            sLogger.Trace("State: Configuring receiver");
            for (auto cmd : commands)
            {
                sLogger.Debug(QString("Progress: Running command: %1").arg(cmd));
                deviceBinaryStream->write("\n\r");
                deviceBinaryStream->write(cmd.toLatin1());
                deviceBinaryStream->write("\n\r");
            }

            std::unique_ptr<DataChunk> dataChunk;
            dataChunk = make_unique<DataChunk>();
            skyPeek = make_unique<SkyPeek>();
            gnssrcvbroker::Webapi::skyPeek = gnssrcvbroker::skyPeek.get();

            auto target = make_unique<DataChunk>();
            
            const int FileIdStdMessageFixedSize = 90;
            const int MsgFmtStdMessageFixedSize = 14;

            auto fileId = make_unique<Greis::FileIdStdMessage>(QString("JP055RLOGF JPS OMNICOLLECT Receiver Log File").leftJustified(FileIdStdMessageFixedSize,' ').toLatin1().constData(),FileIdStdMessageFixedSize);
            auto msgFmt = make_unique<Greis::MsgFmtStdMessage>("MF009JP010109F", MsgFmtStdMessageFixedSize);
            QByteArray bMsgPMVer = QString("PM024rcv/ver/main=\"3.4.1 Mar,13,2012\",@").toLatin1().constData();
            bMsgPMVer.append(QString("%1").arg(Greis::ChecksumComputer::ComputeCs8(bMsgPMVer,bMsgPMVer.size()), 2, 16, QChar('0')).toUpper().toLatin1().constData());
            auto msgPMVer = make_unique<Greis::ParamsStdMessage>(bMsgPMVer,bMsgPMVer.size());

            sLogger.Trace("State: Acquisition is about to begin");
            int msgCounter = 0;
            Message::UniquePtr_t msg;
            Epoch * lastFinishedEpoch;
            while ((msg = messageStream->Next()).get())
            {
                msgCounter++;
                if (msg->Kind() == EMessageKind::StdMessage)
                {
                    auto stdMsg = static_cast<Greis::StdMessage*>(msg.get());
                    sLogger.Debug(QString("Message %1: %2").arg(stdMsg->Id().c_str()).arg(stdMsg->Size()));
                } else {
                    sLogger.Debug(QString("Message nonStdMessage"));
                }

                if(fileTreeMode){
                    //
                }

                if(C["enableParse"].toBool()){
                    skyPeekLock.lockForWrite();
                    skyPeek->AddMessage(msg.get());
                    skyPeekLock.unlock();
                    }
                
                if (msgCounter % 100 == 0)
                {
                    sLogger.Trace(QString("Progress: %1 messages received").arg(msgCounter));
                    if(!dataChunk->Body().empty()){
                        lastFinishedEpoch = dataChunk->Body().back().get();
                        sLogger.Trace(QString("Progress: currently on epoch of %1").arg(lastFinishedEpoch->DateTime.toString(Qt::ISODate)));
                    }
                }
                
            }

            
        }
        catch (Exception& e)
        {
        	sLogger.Error(e.what());
        	return 1;
        }
    }
}
    int main(int argc, char** argv)
	{
		try {
        		QCoreApplication a(argc, argv);
        		QTextCodec* codec = QTextCodec::codecForName("UTF-8");
        		QTextCodec::setCodecForLocale(codec);

        		int LogLevel = 3;
        		sLogger.Initialize(LogLevel);

        		sLogger.Info("omnicollect\\gnssrcvbroker acquisition component");
        		sLogger.Info("Schmidt Institute of Physics of the Earth RAS");
        		sLogger.Info("PROVIDED AS IS, NO WARRANTY, FOR SCIENTIFIC USE");

                QHash<QString,QVariant> C; // Configuration supervariable

     			C["modeSetup"] = false;
     			C["extConfig"] = false;
        		C["enableParse"] = false;
        		C["enableFile"] = false;
                C["enableRB"] = false;
        		C["enableDB"] = false;
        		C["enableWebAPI"] = false;

        		QStringList args = a.arguments();
        		QRegExp rxArgMaint("^-m$|--maintenance");
        		QRegExp rxArgNoParse("--no-parse");
        		QRegExp rxArgNoDB("--no-db");
        		QRegExp rxArgNoFile("--no-file");
        		QRegExp rxArgNoWebAPI("--no-webapi");
        		QRegExp rxArgFile("--file[=]{0,1}(\\S+)");
        		QRegExp rxArgParse("--parse");
        		QRegExp rxArgDB("--db");
                QRegExp rxArgRB("--rb");
        		QRegExp rxArgWebAPI("--webapi");
        		QRegExp rxConfig("--config=(\\S+)");

           		for (int i = 1; i < args.size(); ++i)
        		{
 					if (rxConfig.indexIn(args.at(i)) != -1)
        		   	{
        		   		if(QFileInfo(rxConfig.cap(1)).exists()){
							sIniSettings.Initialize(rxConfig.cap(1));
							C["extConfig"] = true;
					    } else {
					    	sLogger.Fatal("Cannot read the specified config file");
					    	throw 2;
					    }
        		   	}
        		}

        		if(!C["extConfig"].toBool())sIniSettings.Initialize(Path::Combine(Path::ApplicationDirPath(), "config.ini"));
        		C["enableParse"] = sIniSettings.value("enableParse", C["enableParse"].toBool()).toBool();
        		
        		C["greisLogFileName"] = sIniSettings.value("enableFile", QString()).toString();
        		if(C["greisLogFileName"].toString().isEmpty()) C["enableFile"] = false; else C["enableFile"] = true;

        		C["enableDB"] = sIniSettings.value("enableDB", C["enableDB"].toBool()).toBool();
        		
        		C["enableWebAPI"] = sIniSettings.value("enableWebAPI", C["enableWebAPI"].toBool()).toBool();

                C["enableRB"] = sIniSettings.value("enableRB", C["enableRB"].toBool()).toBool();

           		for (int i = 1; i < args.size(); ++i)
        		{
        		    if (rxArgMaint.indexIn(args.at(i)) != -1)
        		    {
        		    	sLogger.Info("Starting in maintenance mode");
        		    	C["modeSetup"] = true;
        		    	C["enableParse"] = false;
        				C["enableFile"] = false;
        				C["enableDB"] = false;
        		    	C["enableWebAPI"] = true;
        		    	break;
        		    }
        		    else if (rxArgNoParse.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableParse"] = false;
        		    }
        		    else if (rxArgNoDB.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableDB"] = false;
        		    }
        		    else if (rxArgNoFile.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableFile"] = false;
        		    }
        		    else if (rxArgNoWebAPI.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableWebAPI"] = false;
        		    }
        		    else if (rxArgFile.indexIn(args.at(i)) != -1)
        		    {
        		        C["greisLogFileName"] = rxArgFile.cap(1);
        		        if(C["greisLogFileName"].toString().isEmpty()) C["enableFile"] = false; else C["enableFile"]=true;
        		    }
        		    else if (rxArgParse.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableParse"] = true;
        		    }
        		    else if (rxArgDB.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableDB"] = true;
        		    }
        		    else if (rxArgWebAPI.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableWebAPI"] = true;
        		    }
                    else if (rxArgRB.indexIn(args.at(i)) != -1)
                    {
                        C["enableRB"] = true;
                    }
        		}

        		C["LogLevel"] = sIniSettings.value("LogLevel", 5).toInt();

        		sLogger.Initialize(C["LogLevel"].toInt());

        		sLogger.Debug("enableParse="+QString::number(C["enableParse"].toBool())); 
        		sLogger.Debug("enableFile="+C["enableFile"].toString());
        		sLogger.Debug("enableDB="+QString::number(C["enableDB"].toBool()));
        		sLogger.Debug("enableWebAPI="+QString::number(C["enableWebAPI"].toBool()));
                sLogger.Debug("enableRB="+QString::number(C["enableRB"].toBool()));

                // WebAPI
                if(C["enableWebAPI"].toBool())
                {
                    gnssrcvbroker::Webapi * webapi = new gnssrcvbroker::Webapi();
                    gnssrcvbroker::Webapi::skyPeekLock = &gnssrcvbroker::skyPeekLock;
                    webapi->start();
                }

                // Start Ringbuffer retention here

                gnssrcvbroker::mainLoop(C);

    }
    catch (Exception& e)
    {
        sLogger.Error(e.what());
        return 1;
    }
    catch (int ErrorCode)
    {
    	return ErrorCode;
    }
}

