#include "mongoose.h"
#include "Common/Logger.h"
#include "Common/SmartPtr.h"
#include "Common/Path.h"
#include "Common/Connection.h"
#include "Greis/DataChunk.h"
using namespace Common;
using namespace Greis;

namespace jpslogd
{
	class Webapi : public QThread {
		Q_OBJECT
		public:
		    void run() override;
		private:
			static void ev_handler(struct mg_connection *c, int ev, void *p);
	};
}
