#include "mongoose.h"
#include "Logger.h"
#include "SmartPtr.h"
#include "Path.h"
#include "webapi.h"
#include <QtCore/QReadWriteLock>
#include <QtCore/QDateTime>
#include <QtCore/QObject>
#include <cmath>


using namespace Common;
namespace btcaster
{
			QReadWriteLock * Webapi::lock_torrent_list = new QReadWriteLock();
			QStringList Webapi::hash_list;
			QStringList Webapi::name_list;
			QStringList Webapi::description_list;
			QString Webapi::channel_name; //TODO: QStringList for multistation support
			QDateTime Webapi::last_updated;
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
			    if (uri=="/status")
			    {
			    	
			    	QString reply;
			    	QDateTime DateTime = QDateTime::currentDateTimeUtc();
			    	reply += QString("{\"%1\":{\n").arg(DateTime.isValid()?DateTime.toString(Qt::ISODate):"\"Unknown\"");
				    reply += QString("}\n}\n");
                    mg_printf(c,
			              "HTTP/1.1 200 OK\r\n"
			              "Content-Type: application/json\r\n"
			              "Content-Length: %d\r\n"
			              "\r\n"
			              "%s",
			              reply.length(), reply.toLatin1().data());
			    } else if (uri=="/rss") {
			    	QString reply;
			    	QDateTime DateTime = QDateTime::currentDateTimeUtc();

						reply += QString("<?xml version=\"1.0\" encoding=\"windows-1251\"?>\n<rss version=\"2.0\">\n<channel>\n<title>btcastd</title>\n");
						reply += QString("<description>station %1</description>\n");
						reply += QString("<pubDate>%1</pubDate>\n").arg(last_updated.toString(Qt::RFC2822Date));
						reply += QString("<lastBuildDate>%1</lastBuildDate>\n").arg(last_updated.toString(Qt::RFC2822Date));
						lock_torrent_list->lockForRead();
						for (int i = 0; i < hash_list.size(); ++i){
							reply += QString("<item><title>%1</title>").arg(name_list.at(i));
							reply += QString("<enclosure url=\"%2\" type=\"application/x-bittorrent\"/>").arg(hash_list.at(i));
							reply += QString("<description>%3</description></item>\n").arg(description_list.at(i));
						}
						reply += QString("</channel></rss>");
						lock_torrent_list->unlock();
				    reply += QString("\n");
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