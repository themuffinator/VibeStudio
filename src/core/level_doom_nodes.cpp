#include "core/level_doom_nodes.h"
#include "core/deflate.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {
// Original bounded readers of the layouts written by ZDBSP processor.cpp,
// bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659 (GPL-2.0-or-later; Randy Heit).
// Formats/field order only; no upstream implementation copied. See CREDITS.md.
constexpr qsizetype nodeByteLimit = 128 * 1024 * 1024;
QString text(const char* value) { return QCoreApplication::translate("LevelDoomNodes", value); }
struct BadNodes {};
struct Cancelled {};
void check(bool valid) {
	if (!valid) {
		throw BadNodes{};
	}
}
void checkpoint(const std::function<bool()>& cancelled) {
	if (cancelled && cancelled()) {
		throw Cancelled{};
	}
}
quint16 u16(QByteArrayView bytes, qsizetype at) {
	check(at >= 0 && at <= bytes.size() - 2);
	return qFromLittleEndian<quint16>(bytes.data() + at);
}
quint32 u32(QByteArrayView bytes, qsizetype at) {
	check(at >= 0 && at <= bytes.size() - 4);
	return qFromLittleEndian<quint32>(bytes.data() + at);
}
struct Cursor {
	QByteArrayView bytes;
	qsizetype at = 0;
	quint32 word() {
		const auto n = u32(bytes, at);
		at += 4;
		return n;
	}
	QByteArrayView records(quint32 count, qsizetype stride) {
		check(at <= bytes.size() && count <= quint64((bytes.size() - at) / stride));
		const auto block = bytes.sliced(at, qsizetype(count) * stride);
		at += block.size();
		return block;
	}
};
void sideReference(const LevelMapDocument& doc, quint32 line, quint32 side) {
	check(line < quint64(doc.doomLinedefs.size()) && side <= 1);
	const auto& native = doc.doomLinedefs[qsizetype(line)];
	check(native.startVertex >= 0 && native.startVertex < doc.doomVertices.size() && native.endVertex >= 0 &&
		  native.endVertex < doc.doomVertices.size());
	const int sidedef = side ? native.backSidedef : native.frontSidedef;
	check(sidedef >= 0 && sidedef < doc.doomSidedefs.size());
	const int sector = doc.doomSidedefs[sidedef].sector;
	check(sector >= 0 && sector < doc.doomSectors.size());
}
void tree(QByteArrayView nodes, quint32 subsectors, int version, const std::function<bool()>& cancel) {
	const qsizetype stride = version == 0 ? 28 : version == 3 ? 40 : 32;
	check(nodes.size() % stride == 0);
	const qsizetype count = nodes.size() / stride;
	if (!count) {
		check(subsectors == 1);
		return;
	}
	const quint32 flag = version == 0 ? 0x8000u : 0x80000000u;
	const auto child = [&](qsizetype n, int side) {
		return version == 0 ? quint32(u16(nodes, n * stride + 24 + 2 * side)) : u32(nodes, n * stride + stride - 8 + 4 * side);
	};
	QVector<quint8> visited(count, 0), leaves(subsectors, 0);
	QVector<QPair<qsizetype, int>> stack{{count - 1, 0}};
	visited[count - 1] = 1;
	while (!stack.isEmpty()) {
		checkpoint(cancel);
		auto& current = stack.last();
		if (current.second == 2) {
			visited[current.first] = 2;
			stack.removeLast();
			continue;
		}
		const auto ref = child(current.first, current.second++), index = ref & ~flag;
		if (ref & flag) {
			check(index < subsectors && !leaves[index]);
			leaves[index] = 1;
		} else {
			check(index < quint64(count) && visited[index] == 0);
			visited[index] = 1;
			stack << qMakePair(qsizetype(index), 0);
		}
	}
	check(std::all_of(visited.cbegin(), visited.cend(), [](auto v) { return v == 2; }) &&
		  std::all_of(leaves.cbegin(), leaves.cend(), [](auto v) { return v == 1; }));
}
void classic(const LevelMapDocument& doc, LevelDoomNodeReport* out, const std::function<bool()>& cancel) {
	const auto segs = doc.doomLumps.value("SEGS"), subs = doc.doomLumps.value("SSECTORS"), nodes = doc.doomLumps.value("NODES");
	check(qint64(segs.size()) + subs.size() + nodes.size() <= nodeByteLimit);
	check(!segs.isEmpty() && !subs.isEmpty() && segs.size() % 12 == 0 && subs.size() % 4 == 0 && nodes.size() % 28 == 0);
	out->segs = quint32(segs.size() / 12);
	out->subsectors = quint32(subs.size() / 4);
	out->nodes = quint32(nodes.size() / 28);
	for (quint32 i = 0; i < out->segs; ++i) {
		if ((i & 255) == 0) {
			checkpoint(cancel);
		}
		check(u16(segs, 12 * i) < doc.doomVertices.size() && u16(segs, 12 * i + 2) < doc.doomVertices.size());
		sideReference(doc, u16(segs, 12 * i + 6), u16(segs, 12 * i + 8));
	}
	QVector<QPair<quint32, quint32>> spans;
	for (quint32 i = 0; i < out->subsectors; ++i) {
		checkpoint(cancel);
		const quint32 count = u16(subs, 4 * i), first = u16(subs, 4 * i + 2);
		check(count && first <= out->segs && count <= out->segs - first);
		spans << qMakePair(first, count);
	}
	std::sort(spans.begin(), spans.end());
	quint32 next = 0;
	for (auto span : spans) {
		check(span.first == next);
		next += span.second;
	}
	check(next == out->segs);
	tree(nodes, out->subsectors, 0, cancel);
}
void runtimeTables(const LevelMapDocument& doc, LevelDoomNodeReport* out, const std::function<bool()>& cancel) {
	const auto blockmap = doc.doomLumps.value("BLOCKMAP");
	if (blockmap.isEmpty()) {
		out->warnings << text(
			QT_TRANSLATE_NOOP("LevelDoomNodes", "BLOCKMAP is empty; testing requires a source port that generates collision blockmaps."));
	} else {
		check(blockmap.size() >= 8 && blockmap.size() % 2 == 0 && blockmap.size() <= nodeByteLimit);
		const quint64 columns = u16(blockmap, 4), rows = u16(blockmap, 6), cells = columns * rows;
		const qsizetype words = blockmap.size() / 2;
		check(columns && rows && cells <= quint64(words - 4));
		// Shared/overlapping block lists are legal. A reverse validity pass keeps
		// malicious overlapping offsets linear instead of rescanning every list.
		QVector<quint8> validTail(words + 1, 0);
		for (qsizetype i = words; i-- > qsizetype(cells) + 4;) {
			if ((i & 255) == 0) {
				checkpoint(cancel);
			}
			const auto line = u16(blockmap, 2 * i);
			validTail[i] = line == 0xffff || (line < doc.doomLinedefs.size() && validTail[i + 1]);
		}
		for (quint64 i = 0; i < cells; ++i) {
			if ((i & 255) == 0) {
				checkpoint(cancel);
			}
			const qsizetype offset = u16(blockmap, 8 + qsizetype(i) * 2);
			check(offset >= qsizetype(cells) + 4 && offset + 1 < words && u16(blockmap, offset * 2) == 0 && validTail[offset + 1]);
		}
	}
	const auto reject = doc.doomLumps.value("REJECT");
	const quint64 sectors = quint64(doc.doomSectors.size());
	if (!reject.isEmpty()) {
		check(quint64(reject.size()) >= (sectors * sectors + 7) / 8);
	}
}
void extended(const LevelMapDocument& doc, QByteArrayView bytes, bool gl, int version, LevelDoomNodeReport* out,
			  const std::function<bool()>& cancel) {
	Cursor cursor{bytes};
	const quint32 original = cursor.word(), added = cursor.word();
	check(original <= quint64(doc.doomVertices.size()) && added <= quint64(nodeByteLimit / 8) &&
		  original <= std::numeric_limits<quint32>::max() - added);
	cursor.records(added, 8);
	out->subsectors = cursor.word();
	check(out->subsectors && out->subsectors <= quint64(nodeByteLimit / 4));
	const auto subs = cursor.records(out->subsectors, 4);
	quint64 count = 0;
	for (quint32 i = 0; i < out->subsectors; ++i) {
		checkpoint(cancel);
		const auto n = u32(subs, 4 * i);
		check(n);
		count += n;
	}
	out->segs = cursor.word();
	check(out->segs && count == out->segs);
	const int segStride = gl && version >= 2 ? 13 : 11;
	const auto segs = cursor.records(out->segs, segStride);
	for (quint32 i = 0; i < out->segs; ++i) {
		if ((i & 255) == 0) {
			checkpoint(cancel);
		}
		const qsizetype at = qsizetype(i) * segStride;
		check(u32(segs, at) < original + added);
		const auto other = u32(segs, at + 4);
		check(gl ? other == 0xffffffffu || other < out->segs : other < original + added);
		const quint32 line = segStride == 13 ? u32(segs, at + 8) : u16(segs, at + 8);
		if (!gl || line != (segStride == 13 ? 0xffffffffu : 0xffffu)) {
			sideReference(doc, line, quint8(segs[at + segStride - 1]));
		}
	}
	out->nodes = cursor.word();
	const auto nodes = cursor.records(out->nodes, version == 3 ? 40 : 32);
	check(cursor.at == bytes.size());
	tree(nodes, out->subsectors, version, cancel);
}
} // namespace

bool isDoomNodeProduct(const QString& name) {
	static const QStringList names{"SEGS",	  "SSECTORS", "NODES",	  "ZNODES",	  "REJECT", "BLOCKMAP",
								   "GL_VERT", "GL_SEGS",  "GL_SSECT", "GL_NODES", "GL_PVS"};
	return names.contains(name.toUpper());
}
LevelDoomNodeReport inspectLevelDoomNodes(const LevelMapDocument& doc, const std::function<bool()>& cancel) {
	LevelDoomNodeReport out;
	if (doc.format != LevelMapFormat::DoomWad) {
		return out;
	}
	if (cancel && cancel()) {
		out.state = LevelDoomNodeState::Cancelled;
		out.message = text(QT_TRANSLATE_NOOP("LevelDoomNodes", "Node validation cancelled."));
		return out;
	}
	if (doc.doomGeometryChanged) {
		out.state = LevelDoomNodeState::Stale;
		out.message = text(QT_TRANSLATE_NOOP("LevelDoomNodes", "Geometry changed. Rebuild nodes before testing this map."));
		return out;
	}
	if (doc.doomNodeReport) {
		return *doc.doomNodeReport;
	}
	const bool udmf = doc.doomFormat == LevelMapDoomFormat::Udmf;
	try {
		checkpoint(cancel);
		bool separateGl = false;
		const auto companion = QStringLiteral("GL_") + doc.mapName.left(5).toUpper();
		for (qsizetype i = 0; i + 1 < doc.doomArchiveLumps.size(); ++i) {
			if ((i & 255) == 0) {
				checkpoint(cancel);
			}
			if (doc.doomArchiveLumps[i].name.compare(companion, Qt::CaseInsensitive) == 0 &&
				doc.doomArchiveLumps[i + 1].name.compare("GL_VERT", Qt::CaseInsensitive) == 0) {
				separateGl = true;
			}
		}
		const bool hexen = doc.doomFormat == LevelMapDoomFormat::Hexen;
		for (const auto& record :
			 {qMakePair("VERTEXES", 4), qMakePair("LINEDEFS", hexen ? 16 : 14), qMakePair("SIDEDEFS", 30), qMakePair("SECTORS", 26)}) {
			if (!udmf) { check(!doc.doomLumps.value(record.first).isEmpty() && doc.doomLumps.value(record.first).size() % record.second == 0); }
		}
		if (udmf) {
			check(doc.doomUdmf != nullptr);
			for (const auto& issue : doc.issues) { check(issue.severity != LevelMapIssueSeverity::Error); }
		}
		auto nodes = doc.doomLumps.value(udmf ? "ZNODES" : "NODES");
		if (udmf && nodes.isEmpty()) {
			out.state = LevelDoomNodeState::Missing;
			out.message = text(QT_TRANSLATE_NOOP("LevelDoomNodes", "Runtime node data is missing. Build nodes before testing this map."));
			return out;
		}
		if (nodes.isEmpty() && !doc.doomLumps.value("ZNODES").isEmpty()) {
			nodes = doc.doomLumps.value("ZNODES");
		}
		const auto glNodes = doc.doomLumps.value("SSECTORS");
		const bool hasGlNodes = glNodes.size() >= 4 && (glNodes[0] == 'X' || glNodes[0] == 'Z') && glNodes.mid(1, 2) == "GL";
		if (nodes.isEmpty() && hasGlNodes) {
			nodes = glNodes;
		}
		check(nodes.size() <= nodeByteLimit);
		const auto magic = nodes.left(4);
		const bool gl = magic.mid(1, 2) == "GL", normal = magic.mid(1) == "NOD";
		const bool encoded = magic.size() == 4 && (magic.startsWith('X') || magic.startsWith('Z')) &&
							 (normal || (gl && (magic[3] == 'N' || magic[3] == '2' || magic[3] == '3')));
		if (udmf && !nodes.isEmpty() && !encoded) { check(false); }
		if (encoded) {
			out.format = QString::fromLatin1(magic);
			QByteArray raw = nodes.mid(4);
			if (magic.startsWith('Z')) {
				const auto decoded = inflateZlib(raw, nodeByteLimit, cancel);
				checkpoint(cancel);
				check(decoded.ok && decoded.bytesConsumed == raw.size());
				raw = decoded.data;
			}
			extended(doc, raw, gl, gl && magic[3] == '3' ? 3 : gl && magic[3] == '2' ? 2 : 1, &out, cancel);
		} else if (nodes.startsWith("xNd4")) {
			out.state = LevelDoomNodeState::Unsupported;
			out.format = "DeePBSP";
			out.message = text(
				QT_TRANSLATE_NOOP("LevelDoomNodes", "This node format is preserved but its runtime structure has not been validated."));
			return out;
		} else if (doc.doomLumps.value("SEGS").isEmpty() || doc.doomLumps.value("SSECTORS").isEmpty()) {
			if (separateGl) {
				out.state = LevelDoomNodeState::Unsupported;
				out.format = QStringLiteral("GL companion");
				out.message = text(QT_TRANSLATE_NOOP(
					"LevelDoomNodes",
					"Only separate GL node data is present. Its structure and source-port compatibility have not been validated."));
				return out;
			}
			out.state = LevelDoomNodeState::Missing;
			out.message = text(QT_TRANSLATE_NOOP("LevelDoomNodes", "Runtime node data is missing. Build nodes before testing this map."));
			return out;
		} else {
			out.format = "classic";
			classic(doc, &out, cancel);
		}
		// ZDBSP can write extended GL nodes to SSECTORS beside normal extended
		// nodes in NODES. Validate both, without treating SSECTORS as 4-byte records.
		if (hasGlNodes && nodes != glNodes) {
			auto glDoc = doc;
			glDoc.doomNodeReport.reset();
			glDoc.doomLumps[udmf ? "ZNODES" : "NODES"] = glNodes;
			const auto glReport = inspectLevelDoomNodes(glDoc, cancel);
			checkpoint(cancel);
			check(glReport.state == LevelDoomNodeState::Present);
			out.format += QStringLiteral(" + ") + glReport.format;
		}
		runtimeTables(doc, &out, cancel);
		if (separateGl) {
			out.warnings << text(
				QT_TRANSLATE_NOOP("LevelDoomNodes", "Separate GL node caches are preserved but have not been structurally validated."));
		}
		out.state = LevelDoomNodeState::Present;
		out.message = text(QT_TRANSLATE_NOOP("LevelDoomNodes", "Node records and references passed structural validation."));
	} catch (const Cancelled&) {
		out.state = LevelDoomNodeState::Cancelled;
		out.message = text(QT_TRANSLATE_NOOP("LevelDoomNodes", "Node validation cancelled."));
	} catch (const BadNodes&) {
		out.state = LevelDoomNodeState::Invalid;
		out.message = text(QT_TRANSLATE_NOOP(
			"LevelDoomNodes", "Node records are malformed or reference missing geometry. Rebuild nodes before testing this map."));
	}
	return out;
}
QString levelDoomNodeStateId(LevelDoomNodeState state) {
	switch (state) {
	case LevelDoomNodeState::NotApplicable:
		return "not-applicable";
	case LevelDoomNodeState::Missing:
		return "missing";
	case LevelDoomNodeState::Invalid:
		return "invalid";
	case LevelDoomNodeState::Present:
		return "present";
	case LevelDoomNodeState::Unsupported:
		return "unsupported";
	case LevelDoomNodeState::Stale:
		return "stale";
	case LevelDoomNodeState::Cancelled:
		return "cancelled";
	}
	return {};
}
QJsonObject levelDoomNodeReportJson(const LevelDoomNodeReport& report) {
	return {{"state", levelDoomNodeStateId(report.state)},
			{"format", report.format},
			{"needsBuild", report.needsBuild()},
			{"message", report.message},
			{"warnings", QJsonArray::fromStringList(report.warnings)},
			{"segs", qint64(report.segs)},
			{"subsectors", qint64(report.subsectors)},
			{"nodes", qint64(report.nodes)}};
}
bool invalidateLevelDoomNodeProducts(QVector<LevelMapWadSourceLump>* lumps, int marker, QStringList* invalidated, QString* error) {
	if (!lumps || marker < 0 || marker >= lumps->size()) {
		return false;
	}
	auto candidate = *lumps;
	const auto name = candidate[marker].name.toUpper();
	const auto glName = "GL_" + name.left(5);
	const bool udmf = marker + 1 < candidate.size() && candidate[marker + 1].name.toUpper() == "TEXTMAP";
	QStringList changed;
	for (qsizetype i = marker + 1; i < candidate.size() && !isLevelDoomMapMarkerAt(candidate, int(i)); ++i) {
		const auto n = candidate[i].name.toUpper();
		if (udmf && n == "ENDMAP") { break; }
		if (isDoomNodeProduct(n)) {
			changed << n;
			candidate[i].bytes.clear();
		} else if (!udmf && (n.startsWith("GL_") || !QStringList{"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SECTORS", "BEHAVIOR", "SCRIPTS",
													   "DIALOGUE", "LIGHTS", "VS_SCENE"}
											   .contains(n))) {
			break;
		}
	}
	QVector<qsizetype> glMarkers;
	int owners = 0;
	for (qsizetype i = 0; i < candidate.size(); ++i) {
		if (isLevelDoomMapMarkerAt(candidate, int(i)) && candidate[i].name.toUpper().left(5) == name.left(5)) {
			++owners;
		}
		if (candidate[i].name.toUpper() == glName && i + 1 < candidate.size() && candidate[i + 1].name.toUpper() == "GL_VERT") {
			glMarkers << i;
		}
	}
	if (!glMarkers.isEmpty() && (glMarkers.size() > 1 || owners != 1)) {
		if (error) {
			*error = text(QT_TRANSLATE_NOOP(
				"LevelDoomNodes",
				"The map has an ambiguous GL node companion. Resolve duplicate map or GL labels before saving geometry."));
		}
		return false;
	}
	if (!glMarkers.isEmpty()) {
		const auto first = glMarkers.first();
		auto last = first + 1;
		while (last < candidate.size() && candidate[last].name.toUpper().startsWith("GL_") && isDoomNodeProduct(candidate[last].name)) {
			changed << candidate[last].name;
			++last;
		}
		candidate.remove(first, last - first);
	}
	changed.removeDuplicates();
	*lumps = std::move(candidate);
	if (invalidated) {
		*invalidated = changed;
	}
	return true;
}
} // namespace vibestudio
