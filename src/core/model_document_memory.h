#pragma once

// Retained-memory accounting for model document history.
//
// Undo states are copies of the document that share every unchanged array
// with one another and with the live state (Qt's implicit sharing): an edit
// that moves vertices in one frame of a 16-frame model detaches that frame's
// positions and nothing else. The ledger counts each heap block once, keyed
// by its address, however many states hold it, so history costs what edits
// actually changed. A block's address identifies it for as long as any state
// in the ledger holds it, because shared blocks are never written in place.

#include "core/model_mesh.h"
#include "core/model_topology.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <type_traits>

namespace vibestudio
{

// Reports every heap block a document state holds, with its size in bytes:
// the element storage of each container, string and image, and of the
// containers inside them. Empty and static (literal) data hold no block.
class ModelMemoryVisitor
{
  public:
	using Block = std::function<void(const void *address, qint64 bytes)>;
	explicit ModelMemoryVisitor(Block block) : m_block(std::move(block)) {}

	template <typename T> void list(const QList<T> &values)
	{
		if (values.capacity() > 0)
		{
			m_block(values.constData(), qint64(values.capacity()) * qint64(sizeof(T)) + header);
		}
	}
	void string(const QString &text)
	{
		if (text.capacity() > 0)
		{
			m_block(text.constData(), qint64(text.capacity()) * qint64(sizeof(QChar)) + header);
		}
	}
	void bytes(const QByteArray &data)
	{
		if (data.capacity() > 0)
		{
			m_block(data.constData(), qint64(data.capacity()) + header);
		}
	}
	void strings(const QStringList &texts)
	{
		list(texts);
		for (const auto &text : texts)
		{
			string(text);
		}
	}
	void image(const QImage &pixels)
	{
		if (!pixels.isNull())
		{
			m_block(pixels.constBits(), pixels.sizeInBytes() + header);
		}
	}
	// A hash table is one bucket index plus its nodes; its first node's
	// address identifies the shared table.
	template <typename K, typename V> void hash(const QHash<K, V> &values)
	{
		if (!values.isEmpty())
		{
			constexpr qint64 node = qint64(sizeof(K)) + (std::is_empty_v<V> ? 0 : qint64(sizeof(V)));
			m_block(&values.constBegin().key(), qint64(values.capacity()) + qint64(values.size()) * node + header);
		}
	}
	template <typename T> void set(const QSet<T> &values)
	{
		if (!values.isEmpty())
		{
			m_block(&*values.constBegin(), qint64(values.capacity()) + qint64(values.size()) * qint64(sizeof(T)) + header);
		}
	}
	// Everything a mesh holds: geometry, frames, skinning, skeleton (joints,
	// clip matrices, bounds and curves), tags, collision, skins and settings.
	void mesh(const ModelMesh &mesh);
	void topology(const ModelSurfaceTopology &topology);

  private:
	// The allocation header Qt keeps before element storage.
	static constexpr qint64 header = 16;
	Block m_block;
};

// The blocks a set of history states holds, with how many states hold each,
// and which of them the live state also holds.
class ModelMemoryLedger
{
  public:
	using Visit = std::function<void(ModelMemoryVisitor &)>;
	void clear();
	// A state entering or leaving history. Remove visits a state exactly as
	// add did, so it must run before that state is destroyed.
	void add(const Visit &visit);
	void remove(const Visit &visit);
	// The state outside history, whose blocks history does not pay for.
	void setLive(const Visit &visit);
	// Bytes of the blocks history holds and the live state does not.
	[[nodiscard]] qint64 retainedBytes() const { return m_total - m_shared; }
	[[nodiscard]] qsizetype blockCount() const { return m_blocks.size(); }

  private:
	struct Entry
	{
		qint64 bytes = 0;
		int references = 0;
	};
	QHash<const void *, Entry> m_blocks;
	QSet<const void *> m_live;
	qint64 m_total = 0, m_shared = 0;
};

} // namespace vibestudio
