#include "core/model_fingerprint.h"
#include "core/model_document.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <bit>

namespace vibestudio
{
namespace
{
class GeometryHash
{
  public:
	GeometryHash(QCryptographicHash &hash, const ModelWorkControl &control, QString *error)
		: m_hash(hash), m_control(control), m_error(error)
	{
	}
	bool word(quint32 value)
	{
		qToLittleEndian(value, m_buffer.data() + m_used);
		m_used += sizeof(value);
		return m_used < m_buffer.size() || flush();
	}
	bool count(qsizetype value) { return word(quint32(quint64(value))) && word(quint32(quint64(value) >> 32)); }
	bool number(float value)
	{
		// JSON treats positive and negative zero as the same value. Preserve that
		// authoring equivalence while encoding finite IEEE float values exactly.
		return word(value == 0 ? 0 : std::bit_cast<quint32>(value));
	}
	bool vector(ModelVec3 value) { return number(value.x) && number(value.y) && number(value.z); }
	bool flush()
	{
		m_completed += m_used;
		if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Serializing, m_completed, 0, m_error))
		{
			return false;
		}
		m_hash.addData(QByteArrayView(m_buffer.data(), qsizetype(m_used)));
		m_used = 0;
		return true;
	}

  private:
	QCryptographicHash &m_hash;
	const ModelWorkControl &m_control;
	QString *m_error;
	std::array<char, 8192> m_buffer{};
	size_t m_used = 0;
	qint64 m_completed = 0;
};
} // namespace

QByteArray modelStateFingerprint(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	// Keep metadata identity tied to the source serializer, including MDL
	// settings, indexed skins, material paths, poses, clips and attachment tags.
	// Geometry is streamed separately: constructing millions of tiny JSON
	// arrays merely to detect an edit is unnecessary allocation and latency.
	ModelMesh metadata = mesh;
	for (auto &surface : metadata.surfaces)
	{
		surface.triangles.clear();
		surface.texCoords.clear();
		surface.uvSeams.clear();
		surface.frames.clear();
	}
	const auto json = editableModelJson(metadata, error, control);
	if (json.isEmpty())
	{
		return {};
	}
	const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	static constexpr char domain[] = "VibeStudio model revision 1";
	hash.addData(QByteArrayView(domain, sizeof(domain)));
	GeometryHash geometry(hash, control, error);
	if (!geometry.count(bytes.size()) || !geometry.flush())
	{
		return {};
	}
	hash.addData(bytes);
	if (!geometry.count(mesh.surfaces.size()))
	{
		return {};
	}
	for (const auto &surface : mesh.surfaces)
	{
		if (!geometry.count(surface.triangles.size()))
		{
			return {};
		}
		for (const auto &triangle : surface.triangles)
		{
			if (!geometry.word(quint32(triangle.a)) || !geometry.word(quint32(triangle.b)) || !geometry.word(quint32(triangle.c)))
			{
				return {};
			}
		}
		if (!geometry.count(surface.texCoords.size()))
		{
			return {};
		}
		for (const auto &uv : surface.texCoords)
		{
			if (!geometry.number(uv.u) || !geometry.number(uv.v))
			{
				return {};
			}
		}
		auto seams = surface.uvSeams.values();
		std::sort(seams.begin(), seams.end());
		if (!geometry.count(seams.size()))
		{
			return {};
		}
		for (auto seam : seams)
		{
			if (!geometry.word(quint32(seam.first)) || !geometry.word(quint32(seam.second)))
			{
				return {};
			}
		}
		if (!geometry.count(surface.frames.size()))
		{
			return {};
		}
		for (const auto &pose : surface.frames)
		{
			if (!geometry.count(pose.positions.size()))
			{
				return {};
			}
			for (auto point : pose.positions)
			{
				if (!geometry.vector(point))
				{
					return {};
				}
			}
			if (!geometry.count(pose.normals.size()))
			{
				return {};
			}
			for (auto normal : pose.normals)
			{
				if (!geometry.vector(normal))
				{
					return {};
				}
			}
		}
	}
	return geometry.flush() ? hash.result() : QByteArray{};
}
} // namespace vibestudio
