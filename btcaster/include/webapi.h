#include "mongoose.h"
#include "Logger.h"
#include "SmartPtr.h"
#include "Path.h"

#include <QtCore/QReadWriteLock>
#include <QtCore/QThread>
#include <QtCore/QObject>
#include <QtCore/QMap>
#include <QtCore/QMapIterator>


using namespace Common;

namespace btcaster
{
	class Webapi : public QThread {
		Q_OBJECT
		public:
		    void run() override;
			static QReadWriteLock * lock_torrent_list ;
			static QStringList hash_list;
			static QStringList name_list;
			static QStringList description_list;
			static QString channel_name; //TODO: QStringList for multistation support
			static QDateTime last_updated;
		private:
			static constexpr char *s_http_port=(char*)"8001"	;
			static void ev_handler(struct mg_connection *c, int ev, void *p);
	};
}
