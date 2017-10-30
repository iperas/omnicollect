#include "mongoose.h"
#include "Common/Logger.h"
#include "Common/SmartPtr.h"
#include "Common/Path.h"
#include "Common/Connection.h"
#include "Greis/DataChunk.h"
#include "webapi.h"
#include <QtCore/QReadWriteLock>
#include "Greis/SkyPeek.h"
#include <cmath>


using namespace Common;
using namespace Greis;

namespace gnssrcvbroker
{
			QReadWriteLock * Webapi::skyPeekLock = NULL;
			SkyPeek * Webapi::skyPeek = NULL;
		    void  Webapi::run()
		    {
		    	sLogger.Debug("[webapi] WebAPI starting");

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
			  	sLogger.Debug(QString("[webapi] Got a request"));
			    struct http_message *hm = (struct http_message *) p;
			    QString uri = QString::fromLocal8Bit(hm->uri.p,hm->uri.len);
			    if (uri=="/SkyPeek")
			    {
			    	skyPeekLock->lockForRead();
			    	QString reply;
			    	QDateTime DateTime = skyPeek->dateTime();
			    	reply += QString("{\"%1\":{\n").arg(DateTime.isValid()?DateTime.toString(Qt::ISODate):"\"NotSetYet\"");
			    	for (int i=0;i< skyPeek->SVs.size(); i++)
                        {
	                        SkyPeek::SV SV = skyPeek->SVs[i];
					    	reply += QString("\"%1\":\n{").arg(SV.USI);
		                    reply += QString("\"PR\": {\"CA/L1\": %2, \"P/L1\": %3, ").arg(SV.PseudorangeC1).arg(SV.Pseudorange1);
		                    reply += QString("\"P/L2\": %1, \"CA/L2\": %2, \"P/L5\": %3},\n").arg(SV.Pseudorange2).arg(SV.PseudorangeC2).arg(SV.Pseudorange5);
		                    reply += QString("\"CP\":{\"CA/L1\": %1, \"P/L1\": %2, \"P/L2\": %3,\n").arg(SV.CarrierPhaseC1).arg(SV.CarrierPhase1).arg(SV.CarrierPhase2);
		                    reply += QString("\"CP CA/L2\": %1, \"CP P/L5\": %2},\n").arg(SV.CarrierPhaseC2).arg(SV.CarrierPhase5);
		                    reply += QString("\"AZ\": %2, \"EL\": %1}").arg(SV.Elevation).arg(SV.Azimuth);
		                    if(i<(skyPeek->SVs.size()-1)) reply += ",\n";
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
			    	QDateTime DateTime = skyPeek->dateTime();
			    	reply += QString("{\"%1\":{\n").arg(DateTime.isValid()?DateTime.toString(Qt::ISODate):"\"NotSetYet\"");
			    	for (int i=0;i< skyPeek->SVs.size(); i++)
                        {
	                        SkyPeek::SV SV = skyPeek->SVs[i];
							if(SV.CarrierPhase1!=0 && SV.CarrierPhase2!=0)
							{
								int PRN = SV.USI;
								double F1=skyPeek->getCarrierFrequency(PRN,1);
								double F2=skyPeek->getCarrierFrequency(PRN,2);
								double TEC = (1.0/40.3)*(pow(F1,2)*pow(F2,2))/(F1-F2)/(F1-F2)*((SV.CarrierPhase1)-(SV.CarrierPhase2));
								reply += QString("\"%1\":\n{\"EL\": %3, \"AZ\": %4, \"TEC\": %2}").arg(SV.USI).arg(TEC).arg(SV.Elevation).arg(SV.Azimuth);
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