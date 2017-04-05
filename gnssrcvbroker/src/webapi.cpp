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
		public:
			static constexpr char *s_http_port=(char*)"8000"	;
		    void run() override
		    {
				struct mg_mgr mgr;
				struct mg_connection *nc;

				mg_mgr_init(&mgr, NULL);
				nc = mg_bind(&mgr, s_http_port, ev_handler);
				mg_set_protocol_http_websocket(nc);
				
				//mg_enable_multithreading(nc);

				for (;;) {
				  mg_mgr_poll(&mgr, 3000);
				}
				mg_mgr_free(&mgr);
		    }
		private:
			static void ev_handler(struct mg_connection *c, int ev, void *p) {
			  if (ev == MG_EV_HTTP_REQUEST) {
			    struct http_message *hm = (struct http_message *) p;
			    char reply[100];

			    /* Send the reply */
			    snprintf(reply, sizeof(reply), "{ \"uri\": \"%.*s\" }\n", (int) hm->uri.len,
			             hm->uri.p);
			    mg_printf(c,
			              "HTTP/1.1 200 OK\r\n"
			              "Content-Type: application/json\r\n"
			              "Content-Length: %d\r\n"
			              "\r\n"
			              "%s",
			              (int) strlen(reply), reply);
			  }
			}
	};
}