#include "pch.h"
#include "rtFoundation.h"
#include "rtSerialization.h"
#include "rtTalkServer.h"
#include "picojson/picojson.h"

namespace rt {

using namespace Poco::Net;
using Poco::URI;

void ServeText(Poco::Net::HTTPServerResponse& response, const std::string& data, int stat, const std::string& mimetype)
{
    response.setStatus((HTTPResponse::HTTPStatus)stat);
    response.setContentType(mimetype);
    response.setContentLength(data.size());

    auto& os = response.send();
    os.write(data.data(), data.size());
    os.flush();
}

void ServeBinary(Poco::Net::HTTPServerResponse& response, RawVector<char>& data, const std::string& mimetype)
{
    response.setStatus(HTTPResponse::HTTPStatus::HTTP_OK);
    response.setContentType(mimetype);
    response.setContentLength(data.size());

    auto& os = response.send();
    os.write(data.data(), data.size());
    os.flush();
}



class TalkServerRequestHandler : public HTTPRequestHandler
{
public:
    TalkServerRequestHandler(TalkServer *server);
    void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override;

private:
    TalkServer *m_server = nullptr;
};

class TalkServerRequestHandlerFactory : public HTTPRequestHandlerFactory
{
public:
    TalkServerRequestHandlerFactory(TalkServer *server);
    HTTPRequestHandler* createRequestHandler(const HTTPServerRequest &request) override;

private:
    TalkServer *m_server = nullptr;
};

TalkServerRequestHandler::TalkServerRequestHandler(TalkServer *server)
    : m_server(server)
{
}

void TalkServerRequestHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response)
{
    URI uri(request.getURI());

    bool handled = false;
    if (uri.getPath() == "/ready") {
        ServeText(response, m_server->ready() ? "1" : "0", HTTPResponse::HTTPStatus::HTTP_OK);
    }
    else if (uri.getPath() == "/talk") {
        auto mes = std::make_shared<TalkServer::TalkMessage>();

        auto qparams = uri.getQueryParameters();
        for (auto& nvp : qparams) {
            if (nvp.first == "text") {
                Poco::URI::decode(nvp.second, mes->text, true);
                mes->text = ToANSI(mes->text.c_str());
            }
#define Decode(N)\
            if (nvp.first == #N) {\
                mes->params.flags.N = 1;\
                mes->params.N = rt::from_string<decltype(mes->params.N)>(nvp.second);\
            }
            rtEachTalkParams(Decode)
#undef Decode
        }

        response.setStatus(HTTPResponse::HTTPStatus::HTTP_OK);
        response.setContentType("application/octet-stream");
        mes->respond_stream = &response.send();

        m_server->addMessage(mes);
        if (mes->wait()) {
            handled = true;
        }
    }
    else if (uri.getPath() == "/stop") {
        auto mes = std::make_shared<TalkServer::StopMessage>();
        m_server->addMessage(mes);
        if (mes->wait())
            handled = true;
        ServeText(response, "ok", HTTPResponse::HTTPStatus::HTTP_OK);
    }
    else if (uri.getPath() == "/params") {
        auto mes = std::make_shared<TalkServer::GetParamsMessage>();
        m_server->addMessage(mes);
        if (mes->wait())
            handled = true;
        ServeText(response, mes->to_json(), HTTPResponse::HTTPStatus::HTTP_OK, "application/json");
    }
#ifdef rtDebug
    else if (uri.getPath() == "/debug") {
        auto mes = std::make_shared<TalkServer::DebugMessage>();
        m_server->addMessage(mes);
        if (mes->wait())
            handled = true;
        ServeText(response, "ok", HTTPResponse::HTTPStatus::HTTP_OK);
    }
#endif

    if (!handled)
        ServeText(response, "", HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
}

TalkServerRequestHandlerFactory::TalkServerRequestHandlerFactory(TalkServer *server)
    : m_server(server)
{
}

HTTPRequestHandler* TalkServerRequestHandlerFactory::createRequestHandler(const HTTPServerRequest&)
{
    return new TalkServerRequestHandler(m_server);
}


bool TalkServer::Message::wait()
{
    for (int i = 0; i < 10000; ++i) {
        if (ready.load())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (task.valid()) {
        task.wait();
    }
    return ready.load();
}

bool TalkServer::Message::isProcessing()
{
    return task.valid() && task.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
}


std::string TalkServer::GetParamsMessage::to_json()
{
    using namespace picojson;
    object ret;
    ret["params"] = rt::to_json(params);
    ret["avators"] = rt::to_json(avators);
    return value(std::move(ret)).serialize(true);
}

bool TalkServer::GetParamsMessage::from_json(const std::string& str)
{
    using namespace picojson;
    value val;
    parse(val, str);

    bool ret = false;
    if (rt::from_json(params, val.get("params")))
        ret = true;
    if (rt::from_json(avators, val.get("avators")))
        ret = true;
    return ret;
}


TalkServer::TalkServer()
{
}

TalkServer::~TalkServer()
{
    stop();
}

void TalkServer::setSettings(const TalkServerSettings& v)
{
    m_settings = v;
}

bool TalkServer::start()
{
    if (!m_server) {
        auto* params = new HTTPServerParams;
        if (m_settings.max_queue > 0)
            params->setMaxQueued(m_settings.max_queue);
        if (m_settings.max_threads > 0)
            params->setMaxThreads(m_settings.max_threads);

        try {
            ServerSocket svs(m_settings.port);
            m_server.reset(new HTTPServer(new TalkServerRequestHandlerFactory(this), svs, params));
            m_server->start();
        }
        catch (Poco::IOException &e) {
            printf("%s\n", e.what());
            return false;
        }
    }

    return true;
}

void TalkServer::stop()
{
    m_server.reset();
}

void TalkServer::processMessages()
{
    lock_t lock(m_mutex);

    bool processing = false;
    for (auto& mes : m_messages) {
        //if (processing) {
        //    if (!dynamic_cast<StopMessage*>(mes.get()))
        //        break;
        //}

        if (!mes->ready.load()) {
            bool handled = true;
            if (auto *talk = dynamic_cast<TalkMessage*>(mes.get()))
                handled = onTalk(*talk);
            else if (auto *stop = dynamic_cast<StopMessage*>(mes.get()))
                handled = onStop(*stop);
            else if (auto *get_params = dynamic_cast<GetParamsMessage*>(mes.get()))
                handled = onGetParams(*get_params);
#ifdef rtDebug
            else if (auto *dbg = dynamic_cast<DebugMessage*>(mes.get()))
                handled = onDebug();
#endif

            if (!handled)
                break;

            mes->ready = true;
        }

        if (!mes->isProcessing())
            mes.reset();
        else
            processing = true;
    }
    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(), [](MessagePtr &p) { return !p; }),
        m_messages.end());
}

void TalkServer::addMessage(MessagePtr mes)
{
    lock_t lock(m_mutex);
    m_messages.push_back(mes);
}

} // namespace rt
