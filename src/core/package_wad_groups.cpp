#include "core/package_wad_groups.h"
#include "core/package_staging.h"
#include "core/package_content.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

namespace vibestudio {
// Private access keeps batch application linear in the number of operations
// and binds reviews to the already captured source/draft content identities.
class PackageWadGroupEditor final {
public:
	static bool append(PackageStagingModel* model, PackageStageOperation operation, QString* error, const PackageReadControl& control) { return model->appendOperation(std::move(operation), error, control); }
	static QByteArray sourceFingerprint(const PackageStagingModel& model)
	{
		const auto base = model.m_baseReader ? model.m_baseReader->fileIdentity() : PackageFileIdentityPtr();
		return (base ? base->sha256 : QByteArray()) + (model.m_draftIdentity ? model.m_draftIdentity->sha256 : QByteArray());
	}
};
namespace {
struct Group {
	qsizetype first = -1, last = -1;
	QString name, kind, error;
};
QString message(const char* source) { return QCoreApplication::translate("VibeStudioPackageSubset", source); }
bool stopped(const PackageReadControl& control, QString* error)
{
	if (!control.isCancelled || !control.isCancelled()) { return false; }
	*error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "WAD group analysis cancelled.")); return true;
}
bool conventionalMap(const QString& name)
{
	static const QRegularExpression pattern(QStringLiteral("^(E[0-9]+M[0-9]+|MAP[0-9]+)$")); return pattern.match(name).hasMatch();
}
const QStringList& binaryMapOrder()
{
	// Format reference: https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/wad.cpp
	// (GPL-2.0-or-later, reviewed 2026-10-04). Original implementation; no copied
	// upstream code. Keep map data together, including source-port sidecars.
	static const QStringList names{QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"),
		QStringLiteral("VERTEXES"), QStringLiteral("SEGS"), QStringLiteral("SSECTORS"), QStringLiteral("NODES"),
		QStringLiteral("SECTORS"), QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP"), QStringLiteral("BEHAVIOR"),
		QStringLiteral("SCRIPTS"), QStringLiteral("ZNODES"), QStringLiteral("DIALOGUE"), QStringLiteral("LIGHTS"), QStringLiteral("VS_SCENE")};
	return names;
}
bool binaryMapLump(const QString& name) { return binaryMapOrder().contains(name); }
const QStringList& glMapOrder()
{
	static const QStringList names{QStringLiteral("GL_VERT"), QStringLiteral("GL_SEGS"), QStringLiteral("GL_SSECT"), QStringLiteral("GL_NODES"), QStringLiteral("GL_PVS")};
	return names;
}
bool glLump(const QString& name) { return glMapOrder().contains(name); }

struct Layout {
	QStringList names;
	QVector<Group> maps, namespaces;
	QVector<qsizetype> owners;
	QSet<qsizetype> namespaceMembers;
};
bool scan(const QStringList& sourceNames, Layout* layout, QString* error, const PackageReadControl& control)
{
	auto& names = layout->names; names.reserve(sourceNames.size());
	for (const auto& name : sourceNames) { if (stopped(control, error)) { return false; } names << name.toUpper(); }
	auto& maps = layout->maps; auto& namespaces = layout->namespaces; auto& owners = layout->owners;
	owners.fill(-1, names.size()); QVector<qsizetype> stack;
	QStringList mapScanNamespaces; auto& namespaceMembers = layout->namespaceMembers;
	for (qsizetype at = 0; at < names.size(); ++at) {
		if (stopped(control, error)) { return false; }
		bool opensNamespace = false;
		const auto family = doomNamespaceMarker(names.at(at), &opensNamespace);
		if (!family.isEmpty()) {
			if (opensNamespace) { mapScanNamespaces << family; }
			else if (!mapScanNamespaces.isEmpty() && mapScanNamespaces.last() == family) { mapScanNamespaces.removeLast(); }
			continue;
		}
		// A flat or patch may legitimately be named THINGS or MAP01. Its
		// enclosing namespace determines its meaning, not the name alone.
		if (!mapScanNamespaces.isEmpty()) { namespaceMembers.insert(at); continue; }
		if (binaryMapLump(names.at(at)) || glLump(names.at(at))
			|| names.at(at) == QStringLiteral("TEXTMAP") || names.at(at) == QStringLiteral("ENDMAP")) { continue; }
		const QString next = at + 1 < names.size() ? names.at(at + 1) : QString();
		const bool udmf = next == QStringLiteral("TEXTMAP");
		const bool gl = names.at(at).startsWith(QStringLiteral("GL_")) && next == QStringLiteral("GL_VERT");
		if (!gl && !conventionalMap(names.at(at)) && !udmf && next != QStringLiteral("THINGS")) { continue; }
		// A map marker can have bytes; map membership is determined by the
		// following run, not by a path sort or the marker's payload length.
		Group group{at, at, names.at(at), gl ? QStringLiteral("gl") : QStringLiteral("map"), {}};
		QSet<QString> found; bool duplicate = false;
		for (qsizetype scan = at + 1; scan < names.size(); ++scan) {
			if (stopped(control, error)) { return false; }
			const QString name = names.at(scan);
			if (udmf) {
				if (scan > at + 1 && (conventionalMap(name) || (scan + 1 < names.size() && names.at(scan + 1) == QStringLiteral("TEXTMAP")))) { break; }
				group.last = scan; if (name == QStringLiteral("ENDMAP")) { found.insert(name); break; }
			} else {
				if (!(gl ? glLump(name) : binaryMapLump(name))) { break; }
				duplicate |= found.contains(name); found.insert(name); group.last = scan;
			}
		}
		const QStringList required = gl ? QStringList{QStringLiteral("GL_VERT"), QStringLiteral("GL_SEGS"), QStringLiteral("GL_SSECT"), QStringLiteral("GL_NODES")}
			: udmf ? QStringList{QStringLiteral("ENDMAP")} : QStringList{QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SECTORS")};
		if (duplicate || std::any_of(required.cbegin(), required.cend(), [&](const auto& name) { return !found.contains(name); })) {
			group.error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Map group %1 is incomplete or repeats a required member. Review the original WAD before editing or exporting this group.")).arg(group.name);
		}
		const qsizetype owner = maps.size(); maps << group;
		for (qsizetype scan = group.first; scan <= group.last; ++scan) { if (stopped(control, error)) { return false; } owners[scan] = owner; }
		at = group.last;
	}
	// Namespace pairing follows the existing reader's marker families. Crossed
	// or unclosed regions cannot authorize a partial export with changed meaning.
	for (qsizetype at = 0; at < names.size(); ++at) {
		if (stopped(control, error)) { return false; }
		if (owners.at(at) >= 0) { continue; }
		bool opens = false; const QString family = doomNamespaceMarker(names.at(at), &opens);
		if (family.isEmpty()) { continue; }
		if (opens) { namespaces.append({at, names.size() - 1, names.at(at), family, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Namespace %1 is not closed.")).arg(names.at(at))}); stack << namespaces.size() - 1; }
		else if (!stack.isEmpty() && namespaces.at(stack.last()).kind == family) {
			auto& group = namespaces[stack.takeLast()]; group.last = at; group.error.clear();
		} else { namespaces.append({at, at, names.at(at), family, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Namespace end marker %1 has no matching open region.")).arg(names.at(at))}); }
	}
	return !stopped(control, error);
}
bool expand(const Layout& layout, const QSet<qsizetype>& requested, QSet<qsizetype>* included,
	QHash<qsizetype, QString>* reasons, QStringList* warnings, QString* error, const PackageReadControl& control)
{
	const auto& names = layout.names; const auto& maps = layout.maps; const auto& namespaces = layout.namespaces;
	const auto& owners = layout.owners; const auto& namespaceMembers = layout.namespaceMembers;
	const auto add = [&](qsizetype index, const QString& why) {
		if (!included->contains(index)) { included->insert(index); reasons->insert(index, why); }
	};
	for (const auto& group : namespaces) {
		if (requested.contains(group.first) || requested.contains(group.last)) {
			if (!group.error.isEmpty()) { *error = group.error; return false; }
			for (qsizetype at = group.first; at <= group.last; ++at) {
				if (stopped(control, error)) { return false; }
				add(at, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Namespace %1")).arg(group.name));
			}
		}
	}
	QSet<qsizetype> selectedMaps;
	for (const qsizetype at : std::as_const(*included)) {
		if (owners.at(at) >= 0) { selectedMaps.insert(owners.at(at)); }
		else if (!namespaceMembers.contains(at) && (binaryMapLump(names.at(at)) || glLump(names.at(at)) || names.at(at) == QStringLiteral("TEXTMAP") || names.at(at) == QStringLiteral("ENDMAP"))) {
			*error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "The selected lump %1 has no complete map group.")).arg(names.at(at)); return false;
		}
	}
	// A separately stored GL run with the same map label belongs with that map.
	// Ambiguous labels are refused rather than attaching another map's nodes.
	const auto initialMaps = selectedMaps;
	for (const qsizetype index : initialMaps) {
		const auto& selected = maps.at(index); const QString peer = selected.kind == QStringLiteral("gl") ? selected.name.mid(3) : QStringLiteral("GL_") + selected.name;
		QVector<qsizetype> matches; qsizetype sameLabel = 0, peerLabelCount = 0;
		for (qsizetype probe = 0; probe < maps.size(); ++probe) {
			if (stopped(control, error)) { return false; }
			if (maps.at(probe).name == peer && maps.at(probe).kind != selected.kind) { matches << probe; }
			if (maps.at(probe).name == peer) { ++peerLabelCount; }
			if (maps.at(probe).name == selected.name) { ++sameLabel; }
		}
		if (matches.size() > 1 || (!matches.isEmpty() && (sameLabel > 1 || peerLabelCount > 1))) {
			*error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Map label %1 has ambiguous companion groups.")).arg(selected.name); return false;
		}
		if (!matches.isEmpty()) { selectedMaps.insert(matches.first()); }
	}
	for (const qsizetype index : selectedMaps) {
		const auto& group = maps.at(index); if (!group.error.isEmpty()) { *error = group.error; return false; }
		for (qsizetype at = group.first; at <= group.last; ++at) {
			if (stopped(control, error)) { return false; }
			add(at, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Map %1")).arg(group.name));
		}
	}
	// Texture directory indices are relative to PNAMES. Keep local tables
	// together; referenced patches may still come from the active base game.
	QVector<qsizetype> textureTables; bool selectedTable = false, pnames = false;
	for (qsizetype at = 0; at < names.size(); ++at) {
		if (stopped(control, error)) { return false; }
		if (owners.at(at) >= 0 || namespaceMembers.contains(at)) { continue; }
		if (names.at(at) == QStringLiteral("PNAMES") || names.at(at) == QStringLiteral("TEXTURE1") || names.at(at) == QStringLiteral("TEXTURE2")) {
			textureTables << at; selectedTable |= included->contains(at); pnames |= names.at(at) == QStringLiteral("PNAMES");
		}
	}
	if (selectedTable) {
		for (const qsizetype at : textureTables) { add(at, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Texture name tables"))); }
		if (!pnames) { *warnings << message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "No local PNAMES table was found; texture definitions require the appropriate base-game table.")); }
	}
	for (const auto& group : namespaces) {
		bool touched = false;
		for (qsizetype at = group.first; at <= group.last; ++at) {
			if (stopped(control, error)) { return false; }
			if (included->contains(at)) { touched = true; break; }
		}
		if (!touched) { continue; }
		if (!group.error.isEmpty()) { *error = group.error; return false; }
		add(group.first, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Namespace %1")).arg(group.name)); add(group.last, message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Namespace %1")).arg(group.name));
	}
	return !stopped(control, error);
}
} // namespace

bool expandPackageWadGroups(const QStringList& names, const QSet<qsizetype>& requested, QSet<qsizetype>* included,
	QHash<qsizetype, QString>* reasons, QStringList* warnings, QString* error, const PackageReadControl& control)
{
	Layout layout; return scan(names, &layout, error, control) && expand(layout, requested, included, reasons, warnings, error, control);
}

bool orderNewPackageDoomLumps(const QStringList& names, QVector<qsizetype>* order, QString* error, const PackageReadControl& control)
{
	QString localError; if (!error) { error = &localError; } error->clear(); if (order) { order->clear(); }
	Layout layout; if (!scan(names, &layout, error, control)) { return false; }
	for (const auto& region : layout.namespaces) { if (stopped(control, error)) { return false; } if (!region.error.isEmpty()) { *error = region.error; return false; } }
	struct Run { QVector<qsizetype> members; bool gl = false; };
	QVector<Run> runs; QVector<qsizetype> binaryRuns, glRuns;
	for (const auto& group : layout.maps) {
		if (stopped(control, error)) { return false; }
		// TEXTMAP through ENDMAP has arbitrary named sidecars, never a binary
		// rank order. Namespace contents are likewise excluded by the scan.
		if (group.first + 1 < layout.names.size() && layout.names.at(group.first + 1) == QStringLiteral("TEXTMAP")) { continue; }
		Run run; run.gl = group.kind == QStringLiteral("gl");
		for (qsizetype at = group.first; at <= group.last; ++at) { if (stopped(control, error)) { return false; } run.members << at; }
		(run.gl ? glRuns : binaryRuns) << runs.size(); runs << run;
	}
	QVector<qsizetype> looseBinary, looseGl, glMarkers;
	for (qsizetype at = 0; at < names.size(); ++at) {
		if (stopped(control, error)) { return false; }
		if (layout.owners.at(at) >= 0 || layout.namespaceMembers.contains(at)) { continue; }
		const auto& name = layout.names.at(at);
		if (binaryMapLump(name)) { looseBinary << at; }
		else if (glLump(name)) { looseGl << at; }
		else if (name.startsWith(QStringLiteral("GL_"))) { glMarkers << at; }
	}
	// A reversed GL run does not begin with GL_VERT yet. Its unique GL_ label
	// is usable only outside every already identified map/namespace region.
	if (!looseGl.isEmpty()) {
		for (const auto marker : glMarkers) { if (stopped(control, error)) { return false; } glRuns << runs.size(); runs.append(Run{{marker}, true}); }
	}
	const auto attach = [&](const QVector<qsizetype>& loose, const QVector<qsizetype>& candidates) {
		if (loose.isEmpty()) { return true; }
		if (candidates.size() != 1) {
			*error = QCoreApplication::translate("VibeStudioPackageWadGroups", "The new WAD has ambiguous or missing map ownership for %1. Add a unique map marker or review the lump order.").arg(names.at(loose.first()));
			return false;
		}
		for (const auto member : loose) {
			if (stopped(control, error)) { return false; }
			runs[candidates.first()].members << member;
		}
		return true;
	};
	if (!attach(looseBinary, binaryRuns) || !attach(looseGl, glRuns)) { return false; }
	QVector<qsizetype> owners(names.size(), -1); QHash<qsizetype, qsizetype> starts;
	for (qsizetype at = 0; at < runs.size(); ++at) {
		if (stopped(control, error)) { return false; }
		auto& run = runs[at]; QSet<QString> seen; qsizetype first = run.members.first();
		for (const auto member : run.members) {
			if (stopped(control, error)) { return false; }
			if (seen.contains(layout.names.at(member))) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "One map cannot hold the lump %1 twice.").arg(layout.names.at(member)); return false;
			}
			seen.insert(layout.names.at(member)); owners[member] = at; first = qMin(first, member);
		}
		const auto& ranks = run.gl ? glMapOrder() : binaryMapOrder();
		std::stable_sort(run.members.begin() + 1, run.members.end(), [&](qsizetype left, qsizetype right) { return ranks.indexOf(layout.names.at(left)) < ranks.indexOf(layout.names.at(right)); });
		starts.insert(first, at);
	}
	QVector<qsizetype> result; result.reserve(names.size());
	for (qsizetype at = 0; at < names.size(); ++at) {
		if (stopped(control, error)) { return false; }
		if (starts.contains(at)) {
			for (const auto member : runs.at(starts.value(at)).members) {
				if (stopped(control, error)) { return false; }
				result << member;
			}
		}
		if (owners.at(at) < 0) { result << at; }
	}
	if (order) { *order = std::move(result); } return true;
}

namespace {
QString editMessage(const char* source) { return QCoreApplication::translate("VibeStudioPackageWadGroups", source); }
QString groupId(const QString& kind, const PackageStagedEntry& entry)
{
	return kind + QLatin1Char(':') + (entry.sourceOrdinal >= 0 ? QString::number(entry.sourceOrdinal) : QStringLiteral("new:") + entry.operationId);
}
QString planFingerprint(const PackageStagingModel& model, const QVector<PackageStagedEntry>& entries, const PackageReadControl& control)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(QJsonDocument(QJsonArray{model.sourcePath(), model.draftPath(), QString::number(model.revision())}).toJson(QJsonDocument::Compact));
	hash.addData(PackageWadGroupEditor::sourceFingerprint(model));
	for (const auto& entry : entries) {
		if (control.isCancelled && control.isCancelled()) { return {}; }
		hash.addData(QJsonDocument(QJsonArray{entry.virtualPath, entry.sourceOrdinal, QString::number(entry.sourceReaderIndex),
			QString::number(entry.sizeBytes), entry.operationId, entry.sourceFilePath, entry.baseVirtualPath, entry.wadInsertBefore, entry.wadNamespace}).toJson(QJsonDocument::Compact));
		if (entry.sourceIdentity) { hash.addData(entry.sourceIdentity->sha256); }
		if (entry.hasInlineBytes) {
			for (qsizetype at = 0; at < entry.inlineBytes.size(); at += 65536) {
				if (control.isCancelled && control.isCancelled()) { return {}; }
				hash.addData(QByteArrayView(entry.inlineBytes).sliced(at, qMin(qsizetype{65536}, entry.inlineBytes.size() - at)));
			}
		}
	}
	return QString::fromLatin1(hash.result().toHex());
}
QStringList entryNames(const QVector<PackageStagedEntry>& entries)
{
	QStringList names; names.reserve(entries.size()); for (const auto& entry : entries) { names << entry.virtualPath; } return names;
}
} // namespace

PackageWadGroupInventory inspectPackageWadGroups(const PackageStagingModel& model, const PackageReadControl& control)
{
	PackageWadGroupInventory result;
	if (stopped(control, &result.error)) { return result; }
	if (!model.isLoaded() || model.sourceFormat() != PackageArchiveFormat::Wad
		|| (model.sourceWadMagic() != QStringLiteral("PWAD") && model.sourceWadMagic() != QStringLiteral("IWAD"))) {
		result.error = editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Map and namespace groups are available in Doom PWAD and IWAD packages.")); return result;
	}
	if (!model.preparePlan(&result.error, control)) { return result; }
	if (!model.summary().canSave) { result.error = model.summary().blockedMessages.join(QLatin1Char('\n')); return result; }
	const auto entries = model.plannedEntries(); Layout layout;
	if (!scan(entryNames(entries), &layout, &result.error, control)) { return result; }
	// A broken namespace makes the meaning of following lumps uncertain.
	for (const auto& group : layout.namespaces) { if (!group.error.isEmpty()) { result.error = group.error; return result; } }
	QHash<QString, QVector<qsizetype>> labels;
	for (qsizetype at = 0; at < layout.maps.size(); ++at) { labels[layout.maps.at(at).name].append(at); }
	for (const auto& map : layout.maps) {
		if (stopped(control, &result.error)) { return result; }
		PackageWadGroup group; group.id = groupId(map.kind, entries.at(map.first)); group.name = map.name; group.kind = map.kind;
		group.error = map.error; group.canRename = true; group.ranges.append({map.first, map.last});
		const auto peerName = map.kind == QStringLiteral("gl") ? map.name.mid(3) : QStringLiteral("GL_") + map.name;
		QVector<qsizetype> peers;
		for (const auto at : labels.value(peerName)) { if (layout.maps.at(at).kind != map.kind) { peers << at; } }
		if (peers.size() > 1 || (!peers.isEmpty() && (labels.value(map.name).size() > 1 || labels.value(peerName).size() > 1))) {
			group.error = editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Map label %1 has ambiguous companion groups.")).arg(map.name);
		} else if (!peers.isEmpty()) {
			const auto& peer = layout.maps.at(peers.first()); group.ranges.append({peer.first, peer.last});
			if (!peer.error.isEmpty()) { group.error = peer.error; }
		}
		result.groups << group;
	}
	for (const auto& region : layout.namespaces) {
		PackageWadGroup group; group.id = groupId(QStringLiteral("namespace"), entries.at(region.first));
		group.name = region.name; group.kind = QStringLiteral("namespace"); group.ranges.append({region.first, region.last}); result.groups << group;
	}
	PackageWadGroup tables; tables.id = QStringLiteral("texture-tables"); tables.name = QStringLiteral("PNAMES / TEXTURE1 / TEXTURE2"); tables.kind = tables.id;
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (stopped(control, &result.error)) { return result; }
		if (layout.owners.at(at) >= 0 || layout.namespaceMembers.contains(at)) { continue; }
		if (QStringList{QStringLiteral("PNAMES"), QStringLiteral("TEXTURE1"), QStringLiteral("TEXTURE2")}.contains(layout.names.at(at))) { tables.ranges.append({at, at}); }
	}
	if (!tables.ranges.isEmpty()) { result.groups << tables; }
	for (auto& group : result.groups) {
		for (const auto& range : group.ranges) { group.memberCount += range.last - range.first + 1; }
	}
	result.fingerprint = planFingerprint(model, entries, control); stopped(control, &result.error); return result;
}

QJsonObject packageWadGroupsJson(const PackageWadGroupInventory& inventory)
{
	QJsonArray groups;
	for (const auto& group : inventory.groups) {
		QJsonArray ranges; for (const auto& range : group.ranges) { ranges.append(QJsonObject{{QStringLiteral("first"), QString::number(range.first)}, {QStringLiteral("last"), QString::number(range.last)}}); }
		groups.append(QJsonObject{{QStringLiteral("id"), group.id}, {QStringLiteral("name"), group.name}, {QStringLiteral("kind"), group.kind},
			{QStringLiteral("memberCount"), QString::number(group.memberCount)}, {QStringLiteral("canRename"), group.canRename && group.error.isEmpty()},
			{QStringLiteral("error"), group.error}, {QStringLiteral("ranges"), ranges}});
	}
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("fingerprint"), inventory.fingerprint}, {QStringLiteral("error"), inventory.error}, {QStringLiteral("groups"), groups}};
}
QJsonObject packageWadGroupEditJson(const PackageWadGroupEditReview& review)
{
	QJsonArray changes;
	for (const auto& change : review.changes) {
		changes.append(QJsonObject{{QStringLiteral("entryIndex"), QString::number(change.entryIndex)}, {QStringLiteral("sourceOrdinal"), change.sourceOrdinal},
			{QStringLiteral("before"), change.before}, {QStringLiteral("after"), change.after}});
	}
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("groupId"), review.groupId}, {QStringLiteral("groupName"), review.groupName},
		{QStringLiteral("operation"), review.remove ? QStringLiteral("delete") : QStringLiteral("rename")}, {QStringLiteral("changes"), changes},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(review.warnings)}};
}

bool stagePackageWadGroupEdit(PackageStagingModel* model, const PackageWadGroupEditRequest& request,
	PackageWadGroupEditReview* review, QString* error, const PackageReadControl& control)
{
	QString localError; if (!error) { error = &localError; } error->clear(); if (review) { *review = {}; }
	const auto fail = [&](const QString& reason) { *error = reason; return false; };
	if (!model || stopped(control, error)) { return false; }
	const auto inventory = inspectPackageWadGroups(*model, control);
	if (!inventory.succeeded()) { return fail(inventory.error); }
	if (request.expectedFingerprint.isEmpty() || request.expectedFingerprint != inventory.fingerprint) {
		return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "The package plan changed. Review its groups again before editing.")));
	}
	const auto found = std::find_if(inventory.groups.cbegin(), inventory.groups.cend(), [&](const auto& group) { return group.id == request.groupId; });
	if (found == inventory.groups.cend()) { return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "The selected WAD group is unavailable."))); }
	const auto& group = *found; if (!group.error.isEmpty()) { return fail(group.error); }
	const auto entries = model->plannedEntries(); QHash<qsizetype, QString> targets;
	if (request.remove) {
		if (!request.targetName.isEmpty()) { return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "A group deletion cannot also rename its members."))); }
		for (const auto& range : group.ranges) {
			for (qsizetype at = range.first; at <= range.last; ++at) { if (stopped(control, error)) { return false; } targets.insert(at, {}); }
		}
	} else {
		if (!group.canRename) { return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Only map groups can be renamed. Namespace and texture table names define their meaning."))); }
		const auto target = request.targetName.toUpper();
		static const QRegularExpression label(QStringLiteral("^[A-Z0-9_][A-Z0-9_-]{0,7}$"));
		if (!label.match(target).hasMatch() || target.startsWith(QStringLiteral("GL_")) || binaryMapLump(target) || glLump(target)
			|| target == QStringLiteral("TEXTMAP") || target == QStringLiteral("ENDMAP") || !doomNamespaceMarker(target).isEmpty()
			|| QStringList{QStringLiteral("PNAMES"), QStringLiteral("TEXTURE1"), QStringLiteral("TEXTURE2")}.contains(target)) {
			return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Choose a non-reserved map label of 1–8 ASCII letters, digits, underscores or hyphens.")));
		}
		for (const auto& range : group.ranges) {
			const bool gl = range.first == group.ranges.first().first ? group.kind == QStringLiteral("gl") : group.kind != QStringLiteral("gl");
			const auto name = gl ? QStringLiteral("GL_") + target : target;
			if (name.size() > 8) { return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "A map with GL nodes needs a label of at most five characters."))); }
			targets.insert(range.first, name);
		}
		for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
			for (qsizetype at = 0; at < entries.size(); ++at) {
				if (stopped(control, error)) { return false; }
				if (at != it.key() && entries.at(at).virtualPath.compare(it.value(), Qt::CaseInsensitive) == 0) {
					return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "The map label already exists: %1")).arg(it.value()));
				}
			}
		}
	}
	PackageWadGroupEditReview prepared{group.id, group.name, request.remove, {}, {}};
	prepared.warnings << editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Map metadata, scripts and asset references are not rewritten. Review dependent content before saving."));
	auto candidate = *model; if (!candidate.beginOperationGroup(request.remove
		? editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Delete WAD group %1")).arg(group.name)
		: editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "Rename WAD map %1")).arg(group.name), error)) { return false; }
	QHash<QString, int> occurrences; for (const auto& entry : entries) { ++occurrences[entry.virtualPath.toCaseFolded()]; }
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (stopped(control, error)) { return false; } if (!targets.contains(at)) { continue; }
		const auto& entry = entries.at(at); const auto target = targets.value(at);
		if (!request.remove && target == entry.virtualPath) { continue; }
		if (entry.sourceOrdinal < 0 && occurrences.value(entry.virtualPath.toCaseFolded()) != 1) {
			return fail(editMessage(QT_TRANSLATE_NOOP("VibeStudioPackageWadGroups", "A newly staged group member is ambiguous: %1")).arg(entry.virtualPath));
		}
		PackageStageOperation operation; operation.type = request.remove ? PackageStageOperationType::Delete : PackageStageOperationType::Rename;
		operation.sourceOrdinal = entry.sourceOrdinal; operation.virtualPath = entry.virtualPath; operation.targetVirtualPath = target;
		if (!PackageWadGroupEditor::append(&candidate, operation, error, control)) { return false; }
		prepared.changes.append({at, entry.sourceOrdinal, entry.virtualPath, target});
	}
	if (!candidate.preparePlan(error, control)) { return false; }
	if (!candidate.summary().canSave) { return fail(candidate.summary().blockedMessages.join(QLatin1Char('\n'))); }
	if (!candidate.verifySources(error, control) || stopped(control, error)) { return false; }
	if (!candidate.endOperationGroup(true, error, control)) { return false; }
	*model = std::move(candidate); if (review) { *review = std::move(prepared); } return true;
}

} // namespace vibestudio
