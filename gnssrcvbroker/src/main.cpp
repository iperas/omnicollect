#include <QtCore/QtCore>
#include <QtCore/QReadWriteLock>
#include <QtCore/QDir>
#include "common/Logger.h"
#include "common/SmartPtr.h"
#include "common/Path.h"
#include "common/Connection.h"
#include "greis/DataChunk.h"
#include "SerialPortBinaryStream.h"
#include "SerialStreamReader.h"
#include "ChainedSink.h"
#include "greis/LoggingBinaryStream.h"
#include "greis/FileBinaryStream.h"
#include "webapi.h"
#include "extras/SkyPeek.h"
#include "greis/StdMessage/FileIdStdMessage.h"
#include "greis/StdMessage/MsgFmtStdMessage.h"
#include "greis/StdMessage/ParamsStdMessage.h"
#include "greis/StdMessage/SatIndexStdMessage.h"
#include "greis/StdMessage/SatNumbersStdMessage.h"
#include "greis/StdMessage/RcvDateStdMessage.h"
#include "greis/ChecksumComputer.h"

using namespace Common;
using namespace Greis;
using namespace Platform;

namespace gnssrcvbroker
{

    QReadWriteLock skyPeekLock;
    std::unique_ptr<SkyPeek> skyPeek;
    
    void writeCRLF( QFile * file)
    {
        file->write(NonStdTextMessage::CreateCarriageReturnMessage()->ToByteArray());
        file->write(NonStdTextMessage::CreateNewLineMessage()->ToByteArray());
    }

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


            sLogger.Debug(QString("serialportName=%1").arg(serialPortName));
            sLogger.Debug(QString("serialPortBaudRate=%1").arg(serialPortBaudRate));
            sLogger.Debug(QString("serialPortFlowControl=%1").arg(serialPortFlowControl));


            Platform::SerialPortBinaryStream::SharedPtr_t deviceBinaryStream;

            bool success = false;

            sLogger.Trace("Progress: Opening port");

            deviceBinaryStream = std::make_shared<Platform::SerialPortBinaryStream>(
                serialPortName, serialPortBaudRate, serialPortFlowControl);     

            sLogger.Trace("State: Stopping current monitoring (if any)...");
            deviceBinaryStream->write("\n\rdm\n\r");
            deviceBinaryStream->write("\r\ndm\r\n");
            deviceBinaryStream->write("\n\rdm\r");
            deviceBinaryStream->write("\r\ndm\n");
            deviceBinaryStream->purgeBuffers();
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
                    C["enableParse"] = true;
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
            deviceBinaryStream->purgeBuffers();
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

            sLogger.Trace("State: Acquisition is about to begin");
            int msgCounter = 0;
            Message::UniquePtr_t msg;
            Epoch * lastFinishedEpoch;
            QFile * fileTreefile;
            QByteArray baFileTreeInitial;
            Greis::FileIdStdMessage::UniquePtr_t fileId = make_unique<Greis::FileIdStdMessage>(QString("JP055RLOGF JPS GNSSRCVBROKER Receiver Log File").leftJustified(FileIdStdMessageFixedSize,' ').toLatin1().constData(),FileIdStdMessageFixedSize); 
            Greis::MsgFmtStdMessage::UniquePtr_t msgFmt = make_unique<Greis::MsgFmtStdMessage>("MF009JP010109F", MsgFmtStdMessageFixedSize);
            QByteArray bMsgPMVer = QString("PM024rcv/ver/main=\"3.7.2 Oct,11,2017\",@").toLatin1().constData();
            bMsgPMVer.append(QString("%1").arg(Greis::ChecksumComputer::ComputeCs8(bMsgPMVer,bMsgPMVer.size()), 2, 16, QChar('0')).toUpper().toLatin1().constData());
            Greis::ParamsStdMessage::UniquePtr_t msgPMVer = make_unique<Greis::ParamsStdMessage>(bMsgPMVer,bMsgPMVer.size());
            while ((msg = messageStream->Next()).get())
            {
                msgCounter++;
                if (msg->Kind() == EMessageKind::StdMessage)
                {
                    auto stdMsg = static_cast<Greis::StdMessage*>(msg.get());
                    sLogger.Debug(QString("Message %1: %2").arg(stdMsg->Id().c_str()).arg(stdMsg->Size()));
                if(C["enableParse"].toBool()){
                    skyPeekLock.lockForWrite();
                    auto id = static_cast<Greis::StdMessage*>(msg.get())->Id();
                    if(id=="JP"){
                        sLogger.Trace(QString("Found stream header [JP]"));
                        fileId = make_unique<Greis::FileIdStdMessage>(msg->ToByteArray(),msg->Size());
                        
                        }
                    if(id=="MF"){
                        sLogger.Trace(QString("Found message format [MF]"));
                        msgFmt= make_unique<Greis::MsgFmtStdMessage>(msg->ToByteArray(),msg->Size());
                        
                        }
                    if(id=="PM"){
                        auto paramMsg = dynamic_cast<ParamsStdMessage*>(msg.get());
                        if(QString::fromStdString(paramMsg->Params()).contains(QString("rcv/ver/main"))){
                            sLogger.Trace(QString("Found firmware version info [PM]"));
                            msgPMVer= make_unique<Greis::ParamsStdMessage>(msg->ToByteArray(),msg->Size());
                            
                            }
                        }
                    skyPeek->AddMessage(msg.get());
                    skyPeekLock.unlock();
                    }
                } else {
                    sLogger.Debug(QString("Message nonStdMessage"));
                }
                if(fileTreeMode){
                    
                    if(skyPeek->DateTime.isValid()){
                    QString path = C["greisLogFileName"].toString()+QString::number(skyPeek->DateTime.date().year())+QDir::separator()+QString::number(skyPeek->DateTime.date().dayOfYear());
                    QDir dirTree(path);
                    if (!dirTree.exists() && !dirTree.mkpath(path)) {
                        sLogger.Error(QString("Cannot create path %1").arg(path));
                        sLogger.Trace("State: Stopping current monitoring (if any)...");
                        deviceBinaryStream->write("\n\rdm\n\r");
                        sLogger.Trace("State: Purging buffers...");
                        deviceBinaryStream->purgeBuffers();
                        throw 73;
                    }
                    int fileNumber = C["greisLogMinuteDenominator"].toInt() * floor((skyPeek->DateTime.time().hour()*60+skyPeek->DateTime.time().minute()) / C["greisLogMinuteDenominator"].toInt());
                    if (baFileTreeInitial.size()!=0) {
                        sLogger.Trace(QString("Creating file %1").arg(path+QDir::separator()+QString::number(fileNumber)+".jps"));
                        fileTreefile = new QFile(path+QDir::separator()+QString::number(fileNumber)+".jps");
                        fileTreefile->open(QIODevice::ReadWrite);
                        sLogger.Trace(QString("Writing %1 bytes of deferred buffer").arg(baFileTreeInitial.size()));
                        fileTreefile->write(baFileTreeInitial);
                        baFileTreeInitial.clear();
                    }
                    
                    sLogger.Debug(QString("Calculated filename: %1").arg((QString::number(fileNumber)+".jps")));
                    sLogger.Debug(QString("Current filename: %1").arg(QFileInfo(fileTreefile->fileName()).fileName()));

                    if (QFileInfo(path+QDir::separator()+QString::number(fileNumber)+".jps")!=QFileInfo(fileTreefile->fileName())){
                        sLogger.Trace(QString("Closing file %1").arg(QFileInfo(fileTreefile->fileName()).filePath()));
                        fileTreefile->close();
                        delete fileTreefile;
                        sLogger.Trace(QString("Creating file %1").arg(path+QDir::separator()+QString::number(fileNumber)+".jps"));
                        fileTreefile = new QFile(path+QDir::separator()+QString::number(fileNumber)+".jps");
                        fileTreefile->open(QIODevice::ReadWrite);
                        fileTreefile->write(fileId->ToByteArray());
                        writeCRLF(fileTreefile);
                        fileTreefile->write(msgFmt->ToByteArray());
                        writeCRLF(fileTreefile);
                        fileTreefile->write(msgPMVer->ToByteArray());
                        writeCRLF(fileTreefile);
                    }
                    fileTreefile->write(msg->ToByteArray());
                    //writeCRLF(fileTreefile);
                    } else { // Defer saving                        
                        baFileTreeInitial.append(msg->ToByteArray());
                        //baFileTreeInitial.append(NonStdTextMessage::CreateNewLineMessage()->ToByteArray());
                        //baFileTreeInitial.append(NonStdTextMessage::CreateCarriageReturnMessage()->ToByteArray());
                    }
                }

                if (msgCounter % 100 == 0)
                {
                    sLogger.Trace(QString("Progress: %1 messages received").arg(msgCounter));
                }
                
            }

            
        }
        catch (Exception& e)
        {
        	sLogger.Error(e.what());
        	return 1;
        }
        return true;
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

                C["greisLogMinuteDenominator"] = sIniSettings.value("fileDenominator", 15).toInt();

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
                sLogger.Debug("fileDenominator="+QString::number(C["greisLogMinuteDenominator"].toInt()));

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

