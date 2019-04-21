#include <QtCore/QtCore>
#include <QtCore/QReadWriteLock>
#include <QtCore/QDir>
#include <QtCore/QFileSystemWatcher>
#include "Logger.h"
#include "SmartPtr.h"
#include "Path.h"
#include "IniSettings.h"
#include "Exception.h"
#include "webapi.h"
#include <QtCore/QObject>
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/file_storage.hpp"


using namespace Common;

namespace btcaster
{
	lt::session ltsession;
	class fileHandler : public QObject
	{
		Q_OBJECT
	private:
	  QSet<QString> Files;
	  QSet<QString> Torrents;
	  QFileSystemWatcher stationWatcher;	
	  QFileSystemWatcher yearWatcher;	
	  QFileSystemWatcher dayWatcher;			
	  QHash<QString,QVariant> C;
	  QString stationPath;
	  QStringList directoryList(const QString &str)
	  {
		  auto path = QDir::cleanPath(str);
		  auto result = path.split(QDir::separator(),QString::SkipEmptyParts);
		  return result;
	  }
	  void printTorrentInfo(libtorrent::torrent_info& t)
	  {
		  sLogger.Debug("Torrent: nodes:");
		  
		  for (auto const &i : t.nodes())
			  sLogger.Debug(QString("Torrent: %1: %2").arg(i.first.c_str()).arg(i.second));

		  sLogger.Debug("Torrent: trackers:");
		  for (auto const &i : t.trackers())
		  	  sLogger.Debug(QString("Torrent: %1: %2").arg(i.tier).arg(i.url.c_str()));

		  std::stringstream ih;
		  ih << t.info_hash();
		  sLogger.Debug(QString("Torrent: number of pieces: %1\n"
					  "Torrent: piece length: %2\n"
					  "Torrent: info hash: %3\n"
					  "Torrent: comment: %4\n"
					  "Torrent: created by: %5\n"
					  "Torrent: magnet link: %6\n"
					  "Torrent: name: %7\n"
					  "Torrent: number of files: %8\n"
					  "Torrent: files:\n").arg(t.num_pieces()).arg(t.piece_length()).arg(ih.str().c_str()).arg(t.comment().c_str()).arg(
					  t.creator().c_str()).arg(make_magnet_uri(t).c_str()).arg(t.name().c_str()).arg(t.num_files()));
		  lt::file_storage const &st = t.files();
		  for (auto const i : st.file_range())
		  {
			  auto const first = st.map_file(i, 0, 0).piece;
			  auto const last = st.map_file(i, std::max(std::int64_t(st.file_size(i)) - 1, std::int64_t(0)), 0).piece;
			  auto const flags = st.file_flags(i);
			  std::stringstream file_hash;
			  if (!st.hash(i).is_all_zeros())
				  file_hash << st.hash(i);
			  sLogger.Debug(QString(" %1" PRIx64 " %2" PRId64 " %3%4%5%6 [ %7, %8 ] %9 %10 %11 %12%13\n").arg( 
				  st.file_offset(i)).arg(st.file_size(i)).arg(((flags & lt::file_storage::flag_pad_file) ? 'p' : '-')).arg(
				   ((flags & lt::file_storage::flag_executable) ? 'x' : '-')).arg(
				    ((flags & lt::file_storage::flag_hidden) ? 'h' : '-')).arg(
					 ((flags & lt::file_storage::flag_symlink) ? 'l' : '-')).arg(
					  static_cast<int>(first)).arg( static_cast<int>(last)).arg( 
					  std::uint32_t(st.mtime(i))).arg( file_hash.str().c_str()).arg( 
					  st.file_path(i).c_str()).arg((flags & lt::file_storage::flag_symlink) ? "-> " : "").arg(
					   (flags & lt::file_storage::flag_symlink) ? st.symlink(i).c_str() : ""));
		  }
		  sLogger.Debug("Torrent: web seeds:");
		  for (auto const &ws : t.web_seeds())
		  {
			  sLogger.Debug(QString("%s %s\n").arg(ws.type == lt::web_seed_entry::url_seed ? "BEP19" : "BEP17",
			   ws.url.c_str()));
		  }
	  }
	bool timeDriftTested = false;
	int compensationMinutes = 1; // Minutes to add to cooldown timer caused by RTC-to-instrument clock skew
	public:
	  fileHandler(QObject *parent, QHash<QString,QVariant> &config)
		  : QObject(parent) {
			this->C = config;
			QObject::connect(&stationWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(handleStation(QString)));
			QObject::connect(&yearWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(handleYear(QString)));
			QObject::connect(&dayWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(handleDay(QString)));
		}

	  ~fileHandler() {}
	public slots:
		void handleStation(const QString& str)
		{
			stationWatcher.addPath(str);
			stationPath = QDir::cleanPath(str);
			sLogger.Info(QString("Directory changed %1 (Station)").arg(str));	
			QDir dir(str);
    		dir.setFilter(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
			dir.setSorting(QDir::Time | QDir::Reversed);
    		QFileInfoList list = dir.entryInfoList();
			for (int i = 0; i < list.size(); ++i) {
				sLogger.Debug(QString("Adding year %1").arg(list.at(i).canonicalFilePath()));
				handleYear(list.at(i).canonicalFilePath()); // Diving into year
				yearWatcher.addPath(list.at(i).canonicalFilePath()); // Adding to monitoring
        		
			}
			
		}
		void handleYear(const QString& str)
		{
			sLogger.Info(QString("Directory changed %1 (Year)").arg(str));	
			QDir dir(str);
    		dir.setFilter(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
			dir.setSorting(QDir::Time | QDir::Reversed);
    		QFileInfoList list = dir.entryInfoList();
			for (int i = 0; i < list.size(); ++i) {
				sLogger.Debug(QString("Adding day %1").arg(list.at(i).canonicalFilePath()));
				handleDay(list.at(i).canonicalFilePath()); // Diving into day
				dayWatcher.addPath(list.at(i).canonicalFilePath()); // Adding to monitoring
        		
			}
			
		}
		void handleDay(const QString& str)
		{
			sLogger.Info(QString("Directory changed %1 (Day)").arg(str));	
			QDir dir(str);
    		dir.setFilter(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot);
			dir.setSorting(QDir::Time | QDir::Reversed);
    		QFileInfoList list = dir.entryInfoList();
			for (int i = 0; i < list.size(); ++i) {
				sLogger.Debug(QString("Adding file %1").arg(list.at(i).canonicalFilePath()));
				handleFile(list.at(i).canonicalFilePath() ); //
        		
			}
			
		}
		void handleFile(QString str)
		{
			sLogger.Debug(QString("Handling file %1").arg(str));	
			QFileInfo fileInfo(str);
			QStringList path = directoryList(str);
			
			int minutes = fileInfo.fileName().split('.')[0].toInt();
			int secsSkew;
			// TODO: handle imcomlete files (e.g. file started after respective denomiator slot but before next one)
			if(!timeDriftTested){
				sLogger.Debug("Checking clock skew");
				sLogger.Debug(QString("Handling file %1: minute %2").arg(fileInfo.fileName()).arg(minutes));	
				int day = path.at(path.size()-2).toInt();
				sLogger.Debug(QString("Handling file %1: day %2").arg(fileInfo.fileName()).arg(day));	
				int year = path.at(path.size()-3).toInt();
				sLogger.Debug(QString("Handling file %1: year %2").arg(fileInfo.fileName()).arg(year));	
				int hours = floor(minutes/60);
				int true_minutes = minutes-(hours*60);
				QDate _qd = QDate();  _qd.setDate(year,1,1); _qd = _qd.addDays(day-1);
				
				QTime _qt = QTime(); _qt.setHMS(hours,true_minutes,0);
				
				QDateTime fileStampTime = QDateTime(); fileStampTime.setDate(_qd); fileStampTime.setTime(_qt);
				sLogger.Debug(QString("Handling file %1: datetime by filename is %2").arg(fileInfo.fileName()).arg(fileStampTime.toString(Qt::ISODate)));	
				QDateTime fileBirthTime = fileInfo.birthTime();	
				sLogger.Debug(QString("Handling file %1: datetime by filesystem is %2").arg(fileInfo.fileName()).arg(fileBirthTime.toString(Qt::ISODate)));	
				secsSkew = abs(fileStampTime.secsTo(fileBirthTime));
				if (secsSkew > 60) {
					sLogger.Info(QString("Instrument-system clock skew is %1 minute(s)").arg(secsSkew));
					compensationMinutes = floor(secsSkew/60);
				} else {
					sLogger.Info(QString("Instrument-system clock skew is imperceptible"));
				}
			}
			// Test if file is not live (i.e. out of current datetime range)
			sLogger.Debug(QString("Checking if file %1 is out of current live data datetime frame").arg(fileInfo.fileName()));	
			QDateTime now = QDateTime::currentDateTime();
			now = now.addSecs(compensationMinutes*60);
			int fileNumber = C["MinuteDenominator"].toInt() * floor((now.time().hour()*60+now.time().minute()) / C["MinuteDenominator"].toInt());
			sLogger.Debug(QString("Checking file %1: current live filename shoud be %2").arg(fileInfo.fileName()).arg(fileNumber));	
			if(fileInfo.size()==0){
				sLogger.Debug(QString("Checking file %1: file is zero, skipping").arg(fileInfo.fileName()));
			} else {
			if(fileNumber != minutes){
				sLogger.Debug(QString("Checking file %1: file is no live, proceeding").arg(fileInfo.fileName()));	
				this->Files.insert(str);
				libtorrent::file_storage fs;
				QString stationName = path.at(path.size()-4);
				//libtorrent::add_files(fs, (stationPath+"README").toStdString());
				//libtorrent::add_files(fs, str.toStdString());
				auto filter = [&](std::string const& p) { 
					QFileInfo _fi(QString::fromStdString(p));
					if(_fi.isDir()) return true;
					if(_fi.isFile() && _fi.fileName()==fileInfo.fileName()) return true;
					//if(_fi.isFile() && _fi.fileName()=="README") return true;
					
					return false; };
				libtorrent::add_files(fs,stationPath.toStdString(),filter);
				fs.set_name(stationName.toStdString());
				libtorrent::create_torrent t(fs);
				sLogger.Debug(QString("Adding file %1: hashing").arg(fileInfo.fileName()));	
				libtorrent::set_piece_hashes(t,(stationPath+QDir::separator()+"..").toStdString());
				std::vector<char> torrent;
				sLogger.Debug(QString("Adding file %1: generating torrent").arg(fileInfo.fileName()));	
				libtorrent::bencode(std::back_inserter(torrent), t.generate());
				sLogger.Debug(QString("Adding file %1: torrent created").arg(fileInfo.fileName()));	
				libtorrent::torrent_info torrent_info(&torrent[0],torrent.size());
				lt::add_torrent_params p;
				p.flags &= lt::add_torrent_params::flag_duplicate_is_error;
				p.save_path = (stationPath+QDir::separator()+"..").toStdString();
				p.ti = std::make_shared<lt::torrent_info>(torrent_info);
				ltsession.add_torrent(p);
				printTorrentInfo(torrent_info);
				sLogger.Debug(QString("Adding file %1: constructing uri").arg(fileInfo.fileName()));	
				QString uri = QString::fromStdString(libtorrent::make_magnet_uri(torrent_info));
				this->Torrents.insert(uri);
			} else {
				sLogger.Debug(QString("Checking file %1: file is live, skipping").arg(fileInfo.fileName()));	
			}
			}
			//sLogger.Debug(QString("Current filelist: \n %1").arg(QStringList(this->Files.values()).join('\n')));	
			sLogger.Debug(QString("Current hashlist: \n %1").arg(QStringList(this->Torrents.values()).join('\n')));	
		}
	
	};


}
#include "main.moc"

    int main(int argc, char** argv)
	{
		try {
        		QCoreApplication a(argc, argv);
        		QTextCodec* codec = QTextCodec::codecForName("UTF-8");
        		QTextCodec::setCodecForLocale(codec);

        		int LogLevel = 3;
        		sLogger.Initialize(LogLevel);

        		sLogger.Info("omnicollect\\btcaster bittorrent cast service");
        		sLogger.Info("Schmidt Institute of Physics of the Earth RAS");
        		sLogger.Info("PROVIDED AS IS, NO WARRANTY, FOR SCIENTIFIC USE");

                QHash<QString,QVariant> C; // Configuration supervariable

        		C["enableFile"] = false;
        		C["enableWebAPI"] = false;

        		QStringList args = a.arguments();
        		QRegExp rxArgMaint("^-m$|--maintenance");
        		QRegExp rxArgNoWebAPI("--no-webapi");
        		QRegExp rxArgFile("--file[=]{0,1}(\\S+)");
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

                C["MinuteDenominator"] = sIniSettings.value("fileDenominator", 15).toInt();

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
        		    else if (rxArgNoWebAPI.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableWebAPI"] = false;
        		    }
        		    else if (rxArgFile.indexIn(args.at(i)) != -1)
        		    {
        		        C["greisLogFileName"] = rxArgFile.cap(1);
        		        if(C["greisLogFileName"].toString().isEmpty()) C["enableFile"] = false; else C["enableFile"]=true;
        		    }
        		    else if (rxArgWebAPI.indexIn(args.at(i)) != -1)
        		    {
        		        C["enableWebAPI"] = true;
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
                    btcaster::Webapi * webapi = new btcaster::Webapi();
                    webapi->start();
                }

				//Scan path and add days to monitoring

				sLogger.Debug(QString("Path: %1").arg(C["greisLogFileName"].toString()));
			 	btcaster::fileHandler* fH = new btcaster::fileHandler(0,C);
				fH->handleStation(C["greisLogFileName"].toString());
    			
				


				a.exec();

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

