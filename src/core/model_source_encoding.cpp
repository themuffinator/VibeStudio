#include "core/model_source_encoding.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace vibestudio
{
namespace
{
// Packed items are runs of 32-bit words; the memory layouts below are the
// file layouts, so packing is a copy (plus a byte swap on big-endian hosts).
static_assert(sizeof(ModelVec3) == 12 && sizeof(ModelTexCoord) == 8 && sizeof(ModelTriangle) == 12);
static_assert(sizeof(ModelJointMatrix) == 48);
static_assert(sizeof(ModelJointInfluence) == 32 && offsetof(ModelJointInfluence, weight) == 4 &&
			  offsetof(ModelJointInfluence, offset) == 8 && offsetof(ModelJointInfluence, normalOffset) == 20);

void swapWords(char *data, qsizetype bytes)
{
	if constexpr (QSysInfo::ByteOrder != QSysInfo::LittleEndian)
	{
		for (qsizetype at = 0; at + 4 <= bytes; at += 4)
		{
			quint32 word;
			std::memcpy(&word, data + at, 4);
			qToLittleEndian(word, data + at);
		}
	}
	else
	{
		Q_UNUSED(data);
		Q_UNUSED(bytes);
	}
}

// Float items store negative zero as zero, as JSON numbers do, so a source
// loads the same model whichever form it was written in. No integer item
// holds the bit pattern of -0 (it would be INT_MIN, never a valid index).
template <typename T> QJsonValue packItems(const T *items, qsizetype count, bool floats)
{
	static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 4 == 0);
	QByteArray bytes(qsizetype(count * qsizetype(sizeof(T))), Qt::Uninitialized);
	if (!bytes.isEmpty())
	{
		std::memcpy(bytes.data(), items, size_t(bytes.size()));
		if (floats)
		{
			for (qsizetype at = 0; at + 4 <= bytes.size(); at += 4)
			{
				quint32 word;
				std::memcpy(&word, bytes.constData() + at, 4);
				if (word == 0x80000000u)
				{
					std::memset(bytes.data() + at, 0, 4);
				}
			}
		}
		swapWords(bytes.data(), bytes.size());
	}
	const auto encoded = bytes.toBase64();
	return QJsonValue(QLatin1StringView(encoded));
}

// The decoded bytes of a canonical base64 string of whole `itemBytes` items,
// at most `limit` of them. Empty optional-like result on failure.
bool unpackBytes(const QJsonValue &value, qsizetype itemBytes, qint64 limit, QByteArray *bytes)
{
	if (!value.isString())
	{
		return false;
	}
	const auto encoded = value.toString().toLatin1();
	auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
	if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || decoded.decoded.size() % itemBytes != 0 ||
		decoded.decoded.size() / itemBytes > limit || encoded.size() != modelSourcePackedLength(decoded.decoded.size()))
	{
		return false;
	}
	swapWords(decoded.decoded.data(), decoded.decoded.size());
	*bytes = std::move(decoded.decoded);
	return true;
}

bool finiteFloats(const QByteArray &bytes, qsizetype firstWord, qsizetype stride)
{
	for (qsizetype word = firstWord; word * 4 < bytes.size(); word += stride)
	{
		float value;
		std::memcpy(&value, bytes.constData() + word * 4, 4);
		if (!std::isfinite(value))
		{
			return false;
		}
	}
	return true;
}

template <typename T> bool unpackItems(const QJsonValue &value, qint64 limit, bool floats, QVector<T> *items)
{
	QByteArray bytes;
	if (!items || !unpackBytes(value, sizeof(T), limit, &bytes) || (floats && !finiteFloats(bytes, 0, 1)))
	{
		return false;
	}
	QVector<T> result(bytes.size() / qsizetype(sizeof(T)));
	if (!result.isEmpty())
	{
		std::memcpy(static_cast<void *>(result.data()), bytes.constData(), size_t(bytes.size()));
	}
	*items = std::move(result);
	return true;
}

int integerLength(qint64 value)
{
	int length = value < 0 ? 2 : 1;
	for (value = value < 0 ? -value : value; value >= 10; value /= 10)
	{
		++length;
	}
	return length;
}
qint64 vectorsLength(const QVector<ModelVec3> &values)
{
	// [x,y,z] and a separating comma per item.
	qint64 length = 2;
	for (const auto &v : values)
	{
		length += modelSourceNumberLength(v.x) + modelSourceNumberLength(v.y) + modelSourceNumberLength(v.z) + 5;
	}
	return length;
}
} // namespace

double modelSourceNumber(float value)
{
	char text[32];
	const auto written = std::to_chars(text, text + sizeof(text), value);
	if (written.ec == std::errc{})
	{
		double shortest = 0;
		const auto read = std::from_chars(text, written.ptr, shortest);
		if (read.ec == std::errc{} && read.ptr == written.ptr &&
			std::bit_cast<quint32>(static_cast<float>(shortest)) == std::bit_cast<quint32>(value))
		{
			return shortest;
		}
	}
	return double(value);
}

int modelSourceNumberLength(float value)
{
	char text[32];
	const auto written = std::to_chars(text, text + sizeof(text), value);
	return written.ec == std::errc{} ? int(written.ptr - text) : 24;
}

QJsonValue packModelSourceVectors(const QVector<ModelVec3> &values) { return packItems(values.constData(), values.size(), true); }
QJsonValue packModelSourceTexCoords(const QVector<ModelTexCoord> &values) { return packItems(values.constData(), values.size(), true); }
QJsonValue packModelSourceTriangles(const QVector<ModelTriangle> &values) { return packItems(values.constData(), values.size(), false); }
QJsonValue packModelSourceIntegers(const QVector<int> &values)
{
	static_assert(sizeof(int) == 4);
	return packItems(values.constData(), values.size(), false);
}
QJsonValue packModelSourceEdges(const QVector<QPair<int, int>> &values)
{
	QVector<int> words;
	words.reserve(values.size() * 2);
	for (const auto &edge : values)
	{
		words << edge.first << edge.second;
	}
	return packModelSourceIntegers(words);
}
QJsonValue packModelSourceMatrices(const QVector<QVector<ModelJointMatrix>> &frames)
{
	QVector<ModelJointMatrix> matrices;
	qsizetype count = 0;
	for (const auto &frame : frames)
	{
		count += frame.size();
	}
	matrices.reserve(count);
	for (const auto &frame : frames)
	{
		matrices += frame;
	}
	return packItems(matrices.constData(), matrices.size(), true);
}
QJsonValue packModelSourceInfluences(const QVector<ModelJointInfluence> &values) { return packItems(values.constData(), values.size(), true); }

bool unpackModelSourceVectors(const QJsonValue &value, qsizetype limit, QVector<ModelVec3> *values)
{
	return unpackItems(value, limit, true, values);
}
bool unpackModelSourceTexCoords(const QJsonValue &value, qsizetype limit, QVector<ModelTexCoord> *values)
{
	return unpackItems(value, limit, true, values);
}
bool unpackModelSourceTriangles(const QJsonValue &value, qsizetype limit, QVector<ModelTriangle> *values)
{
	return unpackItems(value, limit, false, values);
}
bool unpackModelSourceIntegers(const QJsonValue &value, qsizetype limit, QVector<int> *values)
{
	return unpackItems(value, limit, false, values);
}
bool unpackModelSourceEdges(const QJsonValue &value, qsizetype limit, QVector<QPair<int, int>> *values)
{
	QVector<int> words;
	if (!values || !unpackItems(value, limit * 2, false, &words) || words.size() % 2 != 0)
	{
		return false;
	}
	QVector<QPair<int, int>> result;
	result.reserve(words.size() / 2);
	for (qsizetype at = 0; at < words.size(); at += 2)
	{
		result.append({words[at], words[at + 1]});
	}
	*values = std::move(result);
	return true;
}
bool unpackModelSourceMatrices(const QJsonValue &value, int joints, qint64 limit, QVector<QVector<ModelJointMatrix>> *frames)
{
	QVector<ModelJointMatrix> matrices;
	if (!frames || joints <= 0 || !unpackItems(value, limit, true, &matrices) || matrices.size() % joints != 0)
	{
		return false;
	}
	QVector<QVector<ModelJointMatrix>> result;
	result.reserve(matrices.size() / joints);
	for (qsizetype at = 0; at < matrices.size(); at += joints)
	{
		result.append(matrices.mid(at, joints));
	}
	*frames = std::move(result);
	return true;
}
bool unpackModelSourceInfluences(const QJsonValue &value, qsizetype limit, QVector<ModelJointInfluence> *values)
{
	QByteArray bytes;
	// The joint index is an integer; the other seven words are floats.
	if (!values || !unpackBytes(value, sizeof(ModelJointInfluence), limit, &bytes))
	{
		return false;
	}
	for (int word = 1; word < 8; ++word)
	{
		if (!finiteFloats(bytes, word, 8))
		{
			return false;
		}
	}
	QVector<ModelJointInfluence> result(bytes.size() / qsizetype(sizeof(ModelJointInfluence)));
	if (!result.isEmpty())
	{
		std::memcpy(static_cast<void *>(result.data()), bytes.constData(), size_t(bytes.size()));
	}
	*values = std::move(result);
	return true;
}

qint64 modelSourceReadableBulkLength(const ModelMesh &mesh, qint64 stopAbove)
{
	qint64 length = 0;
	for (const auto &surface : mesh.surfaces)
	{
		for (const auto &t : surface.triangles)
		{
			length += integerLength(t.a) + integerLength(t.b) + integerLength(t.c) + 5;
		}
		for (const auto &uv : surface.texCoords)
		{
			length += modelSourceNumberLength(uv.u) + modelSourceNumberLength(uv.v) + 4;
		}
		for (const auto &seam : surface.uvSeams)
		{
			length += integerLength(seam.first) + integerLength(seam.second) + 4;
		}
		for (int identity : surface.topologyVertices)
		{
			length += integerLength(identity) + 1;
		}
		for (const auto &frame : surface.frames)
		{
			length += vectorsLength(frame.positions) + vectorsLength(frame.normals) + 30;
			if (length > stopAbove)
			{
				return length;
			}
		}
		for (const auto &influence : surface.skinning.influences)
		{
			length += integerLength(influence.joint) + modelSourceNumberLength(influence.weight) + modelSourceNumberLength(influence.offset.x) +
					  modelSourceNumberLength(influence.offset.y) + modelSourceNumberLength(influence.offset.z) +
					  modelSourceNumberLength(influence.normalOffset.x) + modelSourceNumberLength(influence.normalOffset.y) +
					  modelSourceNumberLength(influence.normalOffset.z) + 8;
		}
		if (length > stopAbove)
		{
			return length;
		}
	}
	for (const auto &clip : mesh.skeleton.clips)
	{
		for (const auto &pose : clip.frames)
		{
			length += 3;
			for (const auto &matrix : pose)
			{
				for (float value : matrix.m)
				{
					length += modelSourceNumberLength(value) + 1;
				}
			}
			if (length > stopAbove)
			{
				return length;
			}
		}
		length += vectorsLength(clip.frameMins) + vectorsLength(clip.frameMaxs);
	}
	return length;
}

namespace
{
// Characters of a compact JSON array of `count` items whose own characters
// total `items`: brackets and the commas between items.
qint64 arrayLength(qint64 count, qint64 items) { return count > 0 ? items + count + 1 : 2; }
// Characters of one packed array: its base64 in quotes.
qint64 packedString(qint64 bytes) { return modelSourcePackedLength(bytes) + 2; }
// The characters the compact JSON writer prints for numbers, measured in
// chunks with the writer itself, so the count is exact without building the
// whole readable tree.
class ReadableNumbers
{
  public:
	explicit ReadableNumbers(ModelWorkProgress &work) : m_work(work) {}
	bool add(float value)
	{
		m_chunk.append(modelSourceNumber(value));
		return m_chunk.size() < 16384 || flush();
	}
	bool add(const ModelVec3 &value) { return add(value.x) && add(value.y) && add(value.z); }
	// The characters of every number added so far.
	qint64 take()
	{
		flush();
		const auto length = m_length;
		m_length = 0;
		return length;
	}

  private:
	bool flush()
	{
		if (!m_chunk.isEmpty())
		{
			m_length += QJsonDocument(m_chunk).toJson(QJsonDocument::Compact).size() - 1 - m_chunk.size();
			m_chunk = QJsonArray();
		}
		return m_work.check();
	}
	ModelWorkProgress &m_work;
	QJsonArray m_chunk;
	qint64 m_length = 0;
};
// [[x,y,z],…]: three numbers, two commas and brackets per item.
bool vectorsDelta(const QVector<ModelVec3> &values, ReadableNumbers &numbers, qint64 *delta)
{
	for (const auto &value : values)
	{
		if (!numbers.add(value))
		{
			return false;
		}
	}
	*delta += arrayLength(values.size(), 4 * qint64(values.size()) + numbers.take()) - packedString(values.size() * qint64(sizeof(ModelVec3)));
	return true;
}
qint64 integersDelta(const QVector<int> &values)
{
	qint64 items = 0;
	for (int value : values)
	{
		items += integerLength(value);
	}
	return arrayLength(values.size(), items) - packedString(values.size() * 4);
}
} // namespace

bool modelSourceReadableBulkDelta(const ModelMesh &mesh, qint64 *result, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	ReadableNumbers numbers(work);
	qint64 delta = 0;
	for (const auto &surface : mesh.surfaces)
	{
		qint64 items = 0;
		for (const auto &t : surface.triangles)
		{
			items += integerLength(t.a) + integerLength(t.b) + integerLength(t.c) + 4;
		}
		delta += arrayLength(surface.triangles.size(), items) - packedString(surface.triangles.size() * qint64(sizeof(ModelTriangle)));
		for (const auto &uv : surface.texCoords)
		{
			if (!numbers.add(uv.u) || !numbers.add(uv.v))
			{
				return false;
			}
		}
		delta += arrayLength(surface.texCoords.size(), 3 * qint64(surface.texCoords.size()) + numbers.take()) -
				 packedString(surface.texCoords.size() * qint64(sizeof(ModelTexCoord)));
		items = 0;
		for (const auto &seam : surface.uvSeams)
		{
			items += integerLength(seam.first) + integerLength(seam.second) + 3;
		}
		delta += arrayLength(surface.uvSeams.size(), items) - packedString(surface.uvSeams.size() * 8);
		if (!surface.topologyVertices.isEmpty())
		{
			delta += integersDelta(surface.topologyVertices);
		}
		for (const auto &frame : surface.frames)
		{
			if (!vectorsDelta(frame.positions, numbers, &delta) || !vectorsDelta(frame.normals, numbers, &delta))
			{
				return false;
			}
		}
		const auto &skinning = surface.skinning;
		if (skinning.isEmpty())
		{
			continue;
		}
		delta += integersDelta(skinning.count);
		items = 0;
		for (const auto &influence : skinning.influences)
		{
			items += integerLength(influence.joint);
			for (float value : {influence.weight, influence.offset.x, influence.offset.y, influence.offset.z, influence.normalOffset.x,
								influence.normalOffset.y, influence.normalOffset.z})
			{
				if (!numbers.add(value))
				{
					return false;
				}
			}
		}
		delta += arrayLength(8 * qint64(skinning.influences.size()), items + numbers.take()) -
				 packedString(skinning.influences.size() * qint64(sizeof(ModelJointInfluence)));
	}
	for (const auto &clip : mesh.skeleton.clips)
	{
		qint64 poses = 0, matrices = 0;
		for (const auto &pose : clip.frames)
		{
			for (const auto &matrix : pose)
			{
				for (float value : matrix.m)
				{
					if (!numbers.add(value))
					{
						return false;
					}
				}
			}
			poses += arrayLength(12 * qint64(pose.size()), numbers.take());
			matrices += pose.size();
		}
		delta += arrayLength(clip.frames.size(), poses) - packedString(matrices * qint64(sizeof(ModelJointMatrix)));
		if (!clip.frameMins.isEmpty() &&
			(!vectorsDelta(clip.frameMins, numbers, &delta) || !vectorsDelta(clip.frameMaxs, numbers, &delta)))
		{
			return false;
		}
	}
	if (!work.check())
	{
		return false;
	}
	if (result)
	{
		*result = delta;
	}
	return true;
}

qint64 modelSourcePackedSurfaceLength(const ModelSurface &surface)
{
	// The compact JSON writer prints {"normals":"…","positions":"…"} per
	// frame, separated by commas; the other arrays are one string each.
	constexpr qint64 frameObject = sizeof("{\"normals\":\"\",\"positions\":\"\"}") - 1;
	qint64 length = modelSourcePackedLength(surface.triangles.size() * qint64(sizeof(ModelTriangle))) +
					modelSourcePackedLength(surface.texCoords.size() * qint64(sizeof(ModelTexCoord))) +
					modelSourcePackedLength(surface.uvSeams.size() * 8);
	for (const auto &frame : surface.frames)
	{
		length += frameObject + modelSourcePackedLength(frame.positions.size() * qint64(sizeof(ModelVec3))) +
				  modelSourcePackedLength(frame.normals.size() * qint64(sizeof(ModelVec3)));
	}
	return length + std::max<qint64>(0, surface.frames.size() - 1);
}

} // namespace vibestudio
