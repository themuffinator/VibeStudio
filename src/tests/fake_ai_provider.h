#pragma once

// A stand-in AI provider on 127.0.0.1 for tests. It records each HTTP request
// it is sent and answers with a canned status and body: at once, late, or
// never. The AI transport and the Assistant can then be driven end to end
// with no network and no credentials.
//
// Header-only and without Q_OBJECT: the sockets' signals connect to lambdas
// that use the socket as their context, as src/core does.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

#include <memory>

struct FakeAiExchange {
	QByteArray method;
	QByteArray path;
	// Header names in lower case.
	QHash<QByteArray, QByteArray> headers;
	QByteArray body;
};

class FakeAiProvider {
public:
	FakeAiProvider()
	{
		m_server.listen(QHostAddress::LocalHost, 0);
		QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
			while (QTcpSocket* socket = m_server.nextPendingConnection()) {
				serve(socket);
			}
		});
	}

	[[nodiscard]] bool listening() const
	{
		return m_server.isListening();
	}

	[[nodiscard]] quint16 port() const
	{
		return m_server.serverPort();
	}

	// http://127.0.0.1:<port> plus an optional path such as "/v1".
	[[nodiscard]] QString baseUrl(const QString& path = QString()) const
	{
		return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path);
	}

	// How the next requests are answered.
	void answer(int status, const QByteArray& body, int delayMsecs = 0)
	{
		m_status = status;
		m_body = body;
		m_delayMsecs = delayMsecs;
		m_hang = false;
		m_queue.clear();
	}

	// Answers the next requests in turn, one each, then as answer() last set;
	// for clients that ask more than once (a picture per call, a link to fetch,
	// a plan and then its repair).
	void answerInTurn(int status, const QByteArray& body, const QByteArray& contentType = QByteArrayLiteral("application/json"))
	{
		m_queue.push_back({status, body, contentType});
		m_hang = false;
	}

	// Takes requests and never answers them.
	void hang()
	{
		m_hang = true;
	}

	[[nodiscard]] const QVector<FakeAiExchange>& exchanges() const
	{
		return m_exchanges;
	}

	void clear()
	{
		m_exchanges.clear();
	}

	// An OpenAI-compatible Chat Completions answer.
	static QByteArray openAiAnswer(const QString& text, const QString& model = QStringLiteral("fake-model"))
	{
		QByteArray escaped = text.toUtf8();
		escaped.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
		return QByteArrayLiteral("{\"id\":\"chatcmpl-1\",\"object\":\"chat.completion\",\"model\":\"") + model.toUtf8()
			+ QByteArrayLiteral("\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"") + escaped
			+ QByteArrayLiteral("\"},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":42,\"completion_tokens\":7,\"total_tokens\":49}}");
	}

private:
	static QByteArray reasonPhrase(int status)
	{
		switch (status) {
		case 200:
			return "OK";
		case 400:
			return "Bad Request";
		case 401:
			return "Unauthorized";
		case 404:
			return "Not Found";
		case 429:
			return "Too Many Requests";
		case 500:
			return "Internal Server Error";
		default:
			return "Status";
		}
	}

	void serve(QTcpSocket* socket)
	{
		auto buffer = std::make_shared<QByteArray>();
		QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
			buffer->append(socket->readAll());
			const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
			if (headerEnd < 0) {
				return;
			}
			FakeAiExchange exchange;
			const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
			const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
			exchange.method = requestLine.value(0);
			exchange.path = requestLine.value(1);
			for (qsizetype index = 1; index < lines.size(); ++index) {
				const QByteArray line = lines.at(index).trimmed();
				const qsizetype colon = line.indexOf(':');
				if (colon > 0) {
					exchange.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
				}
			}
			const qsizetype length = exchange.headers.value("content-length").toLongLong();
			if (buffer->size() < headerEnd + 4 + length) {
				return;
			}
			exchange.body = buffer->mid(headerEnd + 4, length);
			buffer->clear();
			m_exchanges.push_back(exchange);
			if (m_hang) {
				return;
			}
			int status = m_status;
			QByteArray body = m_body;
			QByteArray contentType = QByteArrayLiteral("application/json");
			if (!m_queue.isEmpty()) {
				const Queued next = m_queue.takeFirst();
				status = next.status;
				body = next.body;
				contentType = next.contentType;
			}
			const QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reasonPhrase(status)
				+ QByteArrayLiteral("\r\nContent-Type: ") + contentType + QByteArrayLiteral("\r\nContent-Length: ") + QByteArray::number(body.size())
				+ QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
			const auto reply = [socket, response]() {
				socket->write(response);
				socket->disconnectFromHost();
			};
			if (m_delayMsecs > 0) {
				QTimer::singleShot(m_delayMsecs, socket, reply);
			} else {
				reply();
			}
		});
		QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	}

	struct Queued {
		int status = 200;
		QByteArray body;
		QByteArray contentType;
	};

	QTcpServer m_server;
	QVector<FakeAiExchange> m_exchanges;
	QVector<Queued> m_queue;
	QByteArray m_body = openAiAnswer(QStringLiteral("OK"));
	int m_status = 200;
	int m_delayMsecs = 0;
	bool m_hang = false;
};
