#include "app/texture_recovery.h"

#include <QJsonDocument>
#include <QThread>
#include <algorithm>

namespace vibestudio {

TextureRecoveryWriter::TextureRecoveryWriter(QString directory, QObject* parent)
	: QObject(parent), m_directory(std::move(directory)) {}

TextureRecoveryWriter::~TextureRecoveryWriter()
{
	finished = {};
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; m_thread = nullptr; }
	// Unexpected destruction keeps the newest accepted draft. Approved closes
	// retire it first. The worker never holds widget or document-owner pointers.
	if (m_pending && !m_retired.contains(m_pending->id)) {
		writeTextureRecovery(m_pending->snapshot, m_directory, m_pending->id);
	}
	removeRetired();
}

bool TextureRecoveryWriter::busy() const { return m_thread || m_pending.has_value(); }
int TextureRecoveryWriter::progress() const { return m_thread && m_result ? m_result->progress.load() : 1000; }

bool TextureRecoveryWriter::checkpoint(const QString& id, TextureRecoverySnapshot snapshot)
{
	if (snapshot.document.strokeActive() || m_retired.contains(id) || textureRecoveryPath(m_directory, id).isEmpty()) { return false; }
	const QByteArray key = QByteArray::number(snapshot.document.revision()) + ':' + QByteArray::number(snapshot.document.activeLayerIndex()) + ':' +
		QJsonDocument(snapshot.metadata).toJson(QJsonDocument::Compact);
	if ((m_pending && m_pending->id == id && m_pending->key == key) ||
		(!m_pending && m_thread && m_result->id == id && m_result->key == key) ||
		(!m_pending && !m_thread && m_savedId == id && m_savedKey == key)) { return false; }
	snapshot.document = snapshot.document.storageSnapshot();
	m_pending = Pending{id, key, std::move(snapshot)};
	startNext(); return true;
}

void TextureRecoveryWriter::retire(const QString& id)
{
	if (id.isEmpty()) { return; }
	if (m_pending && m_pending->id == id) { m_pending.reset(); }
	if (m_savedId == id) { m_savedId.clear(); m_savedKey.clear(); }
	if (m_thread && m_result->id == id) { m_result->cancelled.store(true); }
	m_retired.insert(id); removeRetired();
}

void TextureRecoveryWriter::removeRetired()
{
	for (auto it = m_retired.begin(); it != m_retired.end();) {
		if (m_thread && m_result->id == *it) { ++it; continue; }
		QString error;
		if (removeTextureRecovery(m_directory, *it, &error)) { it = m_retired.erase(it); }
		else { if (finished) { finished(*it, {}, error); } ++it; }
	}
}

void TextureRecoveryWriter::startNext()
{
	if (m_thread || !m_pending) { return; }
	Pending pending = std::move(*m_pending); m_pending.reset();
	auto result = std::make_shared<Result>(); result->id = pending.id; result->key = pending.key; m_result = result;
	m_thread = QThread::create([pending = std::move(pending), directory = m_directory, result]() {
		result->path = writeTextureRecovery(pending.snapshot, directory, pending.id, &result->error, [result](qint64 done, qint64 total) {
			if (result->cancelled.load()) { return false; }
			if (total > 0) { result->progress.store(int(std::clamp<qint64>(done * 1000 / total, 0, 1000))); }
			return true;
		});
	});
	connect(m_thread, &QThread::finished, this, [this, result]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr;
		const bool retired = m_retired.contains(result->id);
		if (!retired && !result->path.isEmpty()) { m_savedId = result->id; m_savedKey = result->key; }
		removeRetired();
		if (!retired && finished) { finished(result->id, result->path, result->error); }
		startNext();
	});
	m_thread->start();
}

} // namespace vibestudio
