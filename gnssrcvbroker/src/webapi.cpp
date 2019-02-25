#include "mongoose.h"
#include "common/Logger.h"
#include "common/SmartPtr.h"
#include "common/Path.h"
#include "common/Connection.h"
#include "greis/DataChunk.h"
#include "webapi.h"
#include <QtCore/QReadWriteLock>
#include "extras/SkyPeek.h"
#include <cmath>


using namespace Common;
using namespace Greis;

namespace gnssrcvbroker
{
			QReadWriteLock * Webapi::skyPeekLock = NULL;
			SkyPeek * Webapi::skyPeek = NULL;
		    void Webapi::run()
		    {
		    	sLogger.Debug("[webapi] Initializing web API");

				struct mg_mgr mgr;
				struct mg_connection *nc;

				mg_mgr_init(&mgr, NULL);

				nc = mg_bind(&mgr, s_http_port, ev_handler);
				sLogger.Debug(QString("[webapi] Binding to port %1").arg(s_http_port));
				mg_set_protocol_http_websocket(nc);
				
				//mg_enable_multithreading(nc);
				sLogger.Debug(QString("[webapi] Entering polling loop"));
				for (;;) {
				  mg_mgr_poll(&mgr, 3000);
				}
				mg_mgr_free(&mgr);
		    }
			void Webapi::ev_handler(struct mg_connection *c, int ev, void *p) {
			  if (ev == MG_EV_HTTP_REQUEST) {
			    struct http_message *hm = (struct http_message *) p;
			    QString uri = QString::fromLocal8Bit(hm->uri.p,hm->uri.len);
				char originatingip[32];
				mg_sock_addr_to_str(&c->sa,originatingip, sizeof(originatingip),MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
				sLogger.Debug(QString("[webapi] %1 %2").arg(originatingip).arg(uri));
			    if (uri=="/SkyPeek")
			    {
			    	skyPeekLock->lockForRead();
			    	QString reply;
			    	QDateTime DateTime = skyPeek->DateTime;
			    	reply += QString("{\"%1\":{\n").arg(DateTime.isValid()?DateTime.toString(Qt::ISODate):"\"Unknown\"");
			    	for (int i=0;i< skyPeek->pSVs.size(); i++)
                        {
	                        SkyPeek::SV SV = skyPeek->pSVs[i];
					    	reply += QString("\"%1\":\n{\"SSID\":%2,\"SVID\":%3,").arg(SV.USI).arg(SV.ESI.SSID).arg(SV.ESI.SVID);
		                    reply += QString("\"PR\": {\"CA/L1\": %2, \"P/L1\": %3, ").arg(SV.Pseudorange[SkyPeek::Signals::caL1],0,'E',10).arg(SV.Pseudorange[SkyPeek::Signals::pL1],0,'E',10);
		                    reply += QString("\"P/L2\": %1, \"CA/L2\": %2, \"P/L5\": %3},\n").arg(SV.Pseudorange[SkyPeek::Signals::pL2],0,'E',10).arg(SV.Pseudorange[SkyPeek::Signals::caL2],0,'E',10).arg(SV.Pseudorange[SkyPeek::Signals::L5],0,'E',10);
		                    reply += QString("\"CP\":{\"CA/L1\": %1, \"P/L1\": %2, \"P/L2\": %3,\n").arg(SV.CarrierPhase[SkyPeek::Signals::caL1],0,'E',10).arg(SV.CarrierPhase[SkyPeek::Signals::pL1],0,'E',10).arg(SV.CarrierPhase[SkyPeek::Signals::pL2],0,'E',10);
		                    reply += QString("\"CA/L2\": %1, \"P/L5\": %2},\n").arg(SV.CarrierPhase[SkyPeek::Signals::caL2],0,'E',10).arg(SV.CarrierPhase[SkyPeek::Signals::L5],0,'E',10);
		                    reply += QString("\"AZ\": %2, \"EL\": %1}").arg(SV.Elevation).arg(SV.Azimuth);
		                    if(i<(skyPeek->pSVs.size()-1)) reply += ",\n";
				        }
				    skyPeekLock->unlock();
				    reply += QString("}\n}\n");
                    mg_printf(c,
			              "HTTP/1.1 200 OK\r\n"
			              "Content-Type: application/json\r\n"
			              "Content-Length: %d\r\n"
			              "\r\n"
			              "%s",
			              reply.length(), reply.toLatin1().data());
			    } else if (uri=="/TEC") {
				    skyPeekLock->lockForRead();
			    	QString reply;
			    	QDateTime DateTime = skyPeek->DateTime;
			    	reply += QString("{\"%1\":{\n").arg(DateTime.isValid()?DateTime.toString(Qt::ISODate):"\"Unknown\"");
			    	for (int i=0;i< skyPeek->SVs.size(); i++)
                        {
	                        SkyPeek::SV SV = skyPeek->SVs[i];
							if(SV.CarrierPhase[SkyPeek::Signals::caL1]!=0 && SV.CarrierPhase[SkyPeek::Signals::caL2]!=0)
							{
								double F1=SV.CarrierFrequency[SkyPeek::Signals::caL1];
								double F2=SV.CarrierFrequency[SkyPeek::Signals::caL2];
								double sTEC = (1.0/40.3)*(pow(F1,2)*pow(F2,2))/(F1-F2)/(F1-F2)*((SV.CarrierPhase[SkyPeek::Signals::caL1])-(SV.CarrierPhase[SkyPeek::Signals::caL2]));
								reply += QString("\"%1\":\n{\"EL\": %3, \"AZ\": %4, \"sTEC\": %2}").arg(SV.USI).arg(sTEC).arg(SV.Elevation).arg(SV.Azimuth);
								if(i<(skyPeek->SVs.size()-1)) reply += ",\n";
							}
				        }
				    reply += QString("}\n}\n");
				    skyPeekLock->unlock();
                    mg_printf(c,
			              "HTTP/1.1 200 OK\r\n"
			              "Content-Type: application/json\r\n"
			              "Content-Length: %d\r\n"
			              "\r\n"
			              "%s",
			              reply.length(), reply.toLatin1().data());		    	
			    } else {
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
			}
}