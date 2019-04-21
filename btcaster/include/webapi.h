#include "mongoose.h"
#include "Logger.h"
#include "SmartPtr.h"
#include "Path.h"

#include <QtCore/QReadWriteLock>
#include <QtCore/QThread>
#include <QtCore/QObject>


using namespace Common;

namespace btcaster
{
	class Webapi : public QThread {
		Q_OBJECT
		public:
		    void run() override;
		private:
			static constexpr char *s_http_port=(char*)"8000"	;
			static void ev_handler(struct mg_connection *c, int ev, void *p);
	};
}
