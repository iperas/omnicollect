#include "mongoose.h"
#include "common/Logger.h"
#include "common/SmartPtr.h"
#include "common/Path.h"
#include "common/Connection.h"
#include "greis/DataChunk.h"
#include <QtCore/QReadWriteLock>
#include "extras/SkyPeek.h"

using namespace Common;
using namespace Greis;

namespace gnssrcvbroker
{
	class Webapi : public QThread {
		Q_OBJECT
		public:
		    void run() override;
			static QReadWriteLock * skyPeekLock ;
			static SkyPeek * skyPeek;
		private:
			static constexpr char *s_http_port=(char*)"8000"	;
			static void ev_handler(struct mg_connection *c, int ev, void *p);
	};
}
