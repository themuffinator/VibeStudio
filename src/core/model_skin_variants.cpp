#include "core/model_skin_variants.h"

#include "core/model_material_slots.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

// Doom 3 skin declarations: id Software DOOM-3 GPL source,
// neo/framework/DeclSkin.cpp (idDeclSkin::Parse, RemapShaderBySkin) and
// neo/framework/DeclManager.cpp (MakeNameCanonical), GPL-3.0-or-later,
// reviewed 2026-10-22. Behaviour only; see docs/CREDITS.md.
// https://github.com/id-Software/DOOM-3/blob/master/neo/framework/DeclSkin.cpp
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelSkinVariants)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool validSurfaceName(const QString &name)
{
	return !name.isEmpty() && name.size() <= 128 &&
		   std::none_of(name.cbegin(), name.cend(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; });
}
bool validMaterial(const QString &material)
{
	return isSafePackageVirtualPath(material) && material.size() <= 255 && !material.contains(QLatin1Char('\\'));
}
// A token the Doom 3 decl writer can quote: printable ASCII without quotes.
bool quotable(const QString &text)
{
	return !text.isEmpty() && std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() >= 32 && c.unicode() < 127 && c != QLatin1Char('"'); });
}
QString quoted(const QString &text)
{
	return QLatin1Char('"') + text + QLatin1Char('"');
}
int surfaceIndex(const ModelMesh &mesh, const QString &name)
{
	for (int i = 0; i < mesh.surfaces.size(); ++i)
		if (mesh.surfaces[i].name == name)
			return i;
	return -1;
}
// The material Doom 3 finds for a name: idDeclManagerLocal::MakeNameCanonical
// (neo/framework/DeclManager.cpp) lowercases it, turns backslashes into
// slashes and cuts it at its last dot, so "Models/X/Body.tga" and
// "models/x/body" are one material.
QString doom3MaterialKey(const QString &name)
{
	QString key;
	key.reserve(name.size());
	int lastDot = -1;
	for (const QChar c : name)
	{
		if (c == QLatin1Char('\\'))
			key += QLatin1Char('/');
		else
		{
			if (c == QLatin1Char('.'))
				lastDot = int(key.size());
			key += c.unicode() < 128 ? c.toLower() : c;
		}
	}
	if (lastDot >= 0)
		key.truncate(lastDot);
	return key;
}
} // namespace

bool validModelSkinVariantName(const QString &name)
{
	static const QRegularExpression id(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,23}$"));
	return id.match(name).hasMatch() && name.compare(QStringLiteral("default"), Qt::CaseInsensitive) != 0;
}

QStringList validateModelSkinVariants(const ModelMesh &mesh)
{
	QStringList errors;
	const auto &variants = mesh.skinVariants;
	if (variants.isEmpty())
		return errors;
	if (!supportsModelMaterialSlots(mesh))
		errors << Text::tr("Skin variants name external materials; this model's skins are embedded. Use its skin groups instead.");
	if (variants.size() > modelSkinVariantMaxCount)
		errors << Text::tr("A model can own at most %1 skin variants.").arg(modelSkinVariantMaxCount);
	QSet<QString> names;
	for (const auto &variant : variants)
	{
		if (!validModelSkinVariantName(variant.name))
			errors << Text::tr("Skin variant name %1 is not valid. Use 1 to 24 ASCII letters, digits, underscores or hyphens, starting with "
							   "a letter; default names the primary materials.")
						  .arg(variant.name);
		else if (names.contains(variant.name.toLower()))
			errors << Text::tr("Two skin variants are named %1. Variant names must differ in more than case.").arg(variant.name);
		names.insert(variant.name.toLower());
		if (variant.bindings.size() > 256)
			errors << Text::tr("Skin variant %1 binds more than 256 surfaces.").arg(variant.name);
		QSet<QString> surfaces;
		for (const auto &binding : variant.bindings)
		{
			if (!validSurfaceName(binding.surface))
				errors << Text::tr("Skin variant %1 binds a surface without a usable name.").arg(variant.name);
			else if (surfaces.contains(binding.surface))
				errors << Text::tr("Skin variant %1 binds surface %2 twice.").arg(variant.name, binding.surface);
			surfaces.insert(binding.surface);
			if (!validMaterial(binding.material))
				errors << Text::tr("Skin variant %1 gives surface %2 the material %3. Use a safe package path of at most 255 characters.")
							  .arg(variant.name, binding.surface, binding.material);
		}
	}
	return errors;
}

QJsonArray modelSkinVariantsJson(const QVector<ModelSkinVariant> &variants)
{
	QJsonArray result;
	for (const auto &variant : variants)
	{
		QJsonArray bindings;
		for (const auto &binding : variant.bindings)
			bindings.append(QJsonObject{{QStringLiteral("surface"), binding.surface}, {QStringLiteral("material"), binding.material}});
		result.append(QJsonObject{{QStringLiteral("name"), variant.name}, {QStringLiteral("bindings"), bindings}});
	}
	return result;
}

bool parseModelSkinVariants(const QJsonValue &value, QVector<ModelSkinVariant> *variants, QString *error)
{
	const auto malformed = [&] { return fail(error, Text::tr("The skin variants of the mesh source are malformed.")); };
	if (!variants || !value.isArray() || value.toArray().size() > modelSkinVariantMaxCount)
		return malformed();
	QVector<ModelSkinVariant> result;
	for (const auto &item : value.toArray())
	{
		const auto object = item.toObject();
		if (!item.isObject() || !object.value(QStringLiteral("name")).isString() || !object.value(QStringLiteral("bindings")).isArray() ||
			object.size() != 2)
			return malformed();
		ModelSkinVariant variant;
		variant.name = object.value(QStringLiteral("name")).toString();
		for (const auto &entry : object.value(QStringLiteral("bindings")).toArray())
		{
			const auto binding = entry.toObject();
			if (!entry.isObject() || binding.size() != 2 || !binding.value(QStringLiteral("surface")).isString() ||
				!binding.value(QStringLiteral("material")).isString())
				return malformed();
			variant.bindings.append({binding.value(QStringLiteral("surface")).toString(), binding.value(QStringLiteral("material")).toString()});
		}
		result.append(std::move(variant));
	}
	ModelMesh check;
	check.skinVariants = result;
	const auto errors = validateModelSkinVariants(check);
	if (!errors.isEmpty())
		return fail(error, errors.first());
	*variants = std::move(result);
	return true;
}

int findModelSkinVariant(const QVector<ModelSkinVariant> &variants, const QString &name)
{
	for (int i = 0; i < variants.size(); ++i)
		if (variants[i].name.compare(name, Qt::CaseInsensitive) == 0)
			return i;
	return -1;
}

QStringList modelSkinVariantMissingSurfaces(const ModelMesh &mesh, const ModelSkinVariant &variant)
{
	QStringList missing;
	for (const auto &binding : variant.bindings)
		if (surfaceIndex(mesh, binding.surface) < 0)
			missing << binding.surface;
	return missing;
}

bool applyModelSkinVariant(const ModelMesh &mesh, const ModelSkinVariant &variant, ModelMesh *result, QString *error, QStringList *inherited)
{
	if (!result)
		return fail(error, Text::tr("No model was given for the skin variant."));
	const auto missing = modelSkinVariantMissingSurfaces(mesh, variant);
	if (!missing.isEmpty())
		return fail(error, Text::tr("Skin variant %1 binds surfaces this model does not have: %2. Update the variant in Skin Variants.")
							   .arg(variant.name, missing.join(QStringLiteral(", "))));
	auto candidate = mesh;
	QSet<int> bound;
	for (const auto &binding : variant.bindings)
	{
		const int index = surfaceIndex(candidate, binding.surface);
		auto &paths = candidate.surfaces[index].skinPaths;
		if (paths.isEmpty())
			paths.append(binding.material);
		else
			paths[0] = binding.material;
		bound.insert(index);
	}
	if (inherited)
	{
		inherited->clear();
		for (int i = 0; i < mesh.surfaces.size(); ++i)
			if (!bound.contains(i))
				*inherited << mesh.surfaces[i].name;
	}
	*result = std::move(candidate);
	return true;
}

bool editModelSkinVariants(const ModelMesh &mesh, const QVector<ModelSkinVariant> &variants, const ModelSkinVariantEdit &edit,
						   QVector<ModelSkinVariant> *result, QString *error)
{
	if (error)
		error->clear();
	if (!result)
		return fail(error, Text::tr("No skin variant output was given."));
	auto candidate = variants;
	const int existing = findModelSkinVariant(candidate, edit.name);
	if (edit.action != ModelSkinVariantAction::Add && existing < 0)
		return fail(error, Text::tr("The model has no skin variant named %1.").arg(edit.name));
	QSet<QString> named;
	for (const auto &binding : edit.bindings)
	{
		// Returning a binding to the primary material also removes one whose
		// surface the model no longer has.
		const bool removal = edit.action == ModelSkinVariantAction::Set && binding.material.isEmpty();
		if (!removal && surfaceIndex(mesh, binding.surface) < 0)
			return fail(error, Text::tr("The model has no surface named %1.").arg(binding.surface));
		if (named.contains(binding.surface))
			return fail(error, Text::tr("Surface %1 is named twice in one change.").arg(binding.surface));
		named.insert(binding.surface);
		if (!binding.material.isEmpty() && !validMaterial(binding.material))
			return fail(error, Text::tr("Material %1 is not a safe package path of at most 255 characters.").arg(binding.material));
	}
	switch (edit.action)
	{
	case ModelSkinVariantAction::Add:
	{
		if (!validModelSkinVariantName(edit.name))
			return fail(error, Text::tr("Use 1 to 24 ASCII letters, digits, underscores or hyphens, starting with a letter. default names the "
										"primary materials."));
		if (existing >= 0)
			return fail(error, Text::tr("The model already has a skin variant named %1.").arg(candidate[existing].name));
		if (candidate.size() >= modelSkinVariantMaxCount)
			return fail(error, Text::tr("A model can own at most %1 skin variants.").arg(modelSkinVariantMaxCount));
		ModelSkinVariant variant{edit.name, {}};
		for (const auto &binding : edit.bindings)
			if (!binding.material.isEmpty())
				variant.bindings.append(binding);
		candidate.append(std::move(variant));
		break;
	}
	case ModelSkinVariantAction::Rename:
	{
		const int other = findModelSkinVariant(candidate, edit.newName);
		if (!validModelSkinVariantName(edit.newName))
			return fail(error, Text::tr("Use 1 to 24 ASCII letters, digits, underscores or hyphens, starting with a letter. default names the "
										"primary materials."));
		if (other >= 0 && other != existing)
			return fail(error, Text::tr("The model already has a skin variant named %1.").arg(candidate[other].name));
		candidate[existing].name = edit.newName;
		break;
	}
	case ModelSkinVariantAction::Remove:
		candidate.removeAt(existing);
		break;
	case ModelSkinVariantAction::Set:
	{
		auto &bindings = candidate[existing].bindings;
		for (const auto &binding : edit.bindings)
		{
			const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto &item) { return item.surface == binding.surface; });
			if (binding.material.isEmpty())
			{
				if (found != bindings.end())
					bindings.erase(found);
			}
			else if (found != bindings.end())
				found->material = binding.material;
			else
				bindings.append(binding);
		}
		break;
	}
	}
	ModelMesh check = mesh;
	check.skinVariants = candidate;
	const auto errors = validateModelSkinVariants(check);
	if (!errors.isEmpty())
		return fail(error, errors.join(QLatin1Char('\n')));
	*result = std::move(candidate);
	return true;
}

QJsonArray modelSkinVariantsReportJson(const ModelMesh &mesh)
{
	QJsonArray result;
	for (const auto &variant : mesh.skinVariants)
	{
		QJsonArray bindings;
		for (const auto &binding : variant.bindings)
		{
			const int index = surfaceIndex(mesh, binding.surface);
			bindings.append(QJsonObject{{QStringLiteral("surface"), binding.surface},
										{QStringLiteral("material"), binding.material},
										{QStringLiteral("primaryMaterial"), index < 0 ? QString() : mesh.surfaces[index].skinPaths.value(0)},
										{QStringLiteral("present"), index >= 0}});
		}
		QStringList inherited;
		ModelMesh applied;
		applyModelSkinVariant(mesh, variant, &applied, nullptr, &inherited);
		result.append(QJsonObject{{QStringLiteral("name"), variant.name},
								  {QStringLiteral("bindings"), bindings},
								  {QStringLiteral("inheritedSurfaces"), QJsonArray::fromStringList(inherited)},
								  {QStringLiteral("missingSurfaces"), QJsonArray::fromStringList(modelSkinVariantMissingSurfaces(mesh, variant))}});
	}
	return result;
}

QStringList modelSkinVariantsReportLines(const ModelMesh &mesh)
{
	QStringList lines;
	if (mesh.skinVariants.isEmpty())
		return {Text::tr("No skin variants; every skin uses the primary materials.")};
	lines << Text::tr("%n skin variant(s):", nullptr, int(mesh.skinVariants.size()));
	for (const auto &variant : mesh.skinVariants)
	{
		lines << Text::tr("%1: %n surface(s) bound", nullptr, int(variant.bindings.size())).arg(variant.name);
		for (const auto &binding : variant.bindings)
		{
			const int index = surfaceIndex(mesh, binding.surface);
			lines << (index < 0 ? Text::tr("  %1 → %2 (the model has no such surface)").arg(binding.surface, binding.material)
								: Text::tr("  %1: %2 → %3").arg(binding.surface, mesh.surfaces[index].skinPaths.value(0), binding.material));
		}
	}
	return lines;
}

QString modelDoom3SkinDeclName(const QString &modelPath, const QString &variant)
{
	auto base = modelPath;
	base.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int slash = base.lastIndexOf(QLatin1Char('/'));
	const int dot = base.lastIndexOf(QLatin1Char('.'));
	if (dot > slash)
		base.truncate(dot);
	if (base.isEmpty())
		base = QStringLiteral("model");
	auto name = QStringLiteral("skins/%1_%2").arg(base, variant);
	for (auto &c : name)
		if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != QLatin1Char('_') && c != QLatin1Char('/') && c != QLatin1Char('.'))
			c = QLatin1Char('_');
	return name;
}

bool writeModelDoom3Skin(const ModelMesh &mesh, const ModelSkinVariant &variant, const QString &declName, const QString &modelPath,
						 QByteArray *bytes, QString *error)
{
	if (error)
		error->clear();
	static const QRegularExpression bare(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_/.]{0,254}$"));
	if (!bytes)
		return fail(error, Text::tr("No output was given for the Doom 3 skin."));
	if (!bare.match(declName).hasMatch())
		return fail(error, Text::tr("Use a declaration name of ASCII letters, digits, underscores, slashes and dots, such as skins/models/prop_red."));
	if (!modelPath.isEmpty() && !quotable(modelPath))
		return fail(error, Text::tr("The model path %1 cannot be written to a Doom 3 skin. Use printable ASCII without quotes.").arg(modelPath));
	const auto missing = modelSkinVariantMissingSurfaces(mesh, variant);
	if (!missing.isEmpty())
		return fail(error, Text::tr("Skin variant %1 binds surfaces this model does not have: %2. Update the variant in Skin Variants.")
							   .arg(variant.name, missing.join(QStringLiteral(", "))));
	// Materials are compared as Doom 3 finds them (doom3MaterialKey), so names
	// that differ only in case, slashes or an extension are one material.
	QVector<ModelDoom3SkinMapping> mappings;
	QHash<QString, QString> mapped, mappedBy, mappedFrom;
	for (const auto &binding : variant.bindings)
	{
		const int index = surfaceIndex(mesh, binding.surface);
		const auto from = mesh.surfaces[index].skinPaths.value(0);
		if (from.isEmpty())
			return fail(error, Text::tr("Surface %1 has no primary material for a Doom 3 skin to replace. Assign one first.").arg(binding.surface));
		if (!quotable(from) || !quotable(binding.material))
			return fail(error, Text::tr("The materials of surface %1 cannot be written to a Doom 3 skin. Use printable ASCII without quotes.")
								   .arg(binding.surface));
		const auto key = doom3MaterialKey(from);
		if (key == doom3MaterialKey(binding.material))
			continue;
		const auto previous = mapped.constFind(key);
		if (previous != mapped.cend())
		{
			if (doom3MaterialKey(*previous) != doom3MaterialKey(binding.material))
				return fail(error, Text::tr("Surfaces %1 and %2 both use %3, and a Doom 3 skin maps materials, not surfaces, so it cannot give them "
											"different materials. Give one surface its own primary material or the same variant material.")
									   .arg(mappedBy.value(key), binding.surface, mappedFrom.value(key)));
			continue;
		}
		mapped.insert(key, binding.material);
		mappedBy.insert(key, binding.surface);
		mappedFrom.insert(key, from);
		mappings.append({from, binding.material});
	}
	if (mappings.isEmpty())
		return fail(error, Text::tr("Skin variant %1 changes no material, so its Doom 3 skin would be empty.").arg(variant.name));
	for (const auto &surface : mesh.surfaces)
	{
		const bool bound = std::any_of(variant.bindings.cbegin(), variant.bindings.cend(), [&](const auto &b) { return b.surface == surface.name; });
		const auto material = surface.skinPaths.value(0);
		const auto key = doom3MaterialKey(material);
		if (!bound && !material.isEmpty() && mapped.contains(key))
			return fail(error, Text::tr("Surface %1 keeps %2 in variant %3, but a Doom 3 skin would change it to %4 with surface %5, which shares "
										"the material. Bind %1 in the variant too, or give it its own primary material.")
								   .arg(surface.name, material, variant.name, mapped.value(key), mappedBy.value(key)));
	}
	QString text = QStringLiteral("// Doom 3 skin declaration written by VibeStudio: variant %1.\n").arg(variant.name);
	text += QStringLiteral("skin %1\n{\n").arg(declName);
	if (!modelPath.isEmpty())
		text += QStringLiteral("\tmodel\t%1\n").arg(quoted(modelPath));
	for (const auto &mapping : std::as_const(mappings))
		text += QStringLiteral("\t%1\t%2\n").arg(quoted(mapping.from), quoted(mapping.to));
	text += QStringLiteral("}\n");
	*bytes = text.toLatin1();
	return true;
}

bool parseModelDoom3Skins(const QByteArray &bytes, QVector<ModelDoom3Skin> *skins, QString *error)
{
	if (error)
		error->clear();
	if (!skins || bytes.size() > 16 * 1024 * 1024)
		return fail(error, Text::tr("Choose a declaration file of at most 16 MiB."));
	QStringList tokens;
	QVector<bool> quotedTokens;
	qsizetype at = 0;
	while (at < bytes.size())
	{
		const char c = bytes[at];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			++at;
			continue;
		}
		if (bytes.mid(at, 2) == "//")
		{
			while (at < bytes.size() && bytes[at] != '\n')
				++at;
			continue;
		}
		if (bytes.mid(at, 2) == "/*")
		{
			const auto end = bytes.indexOf("*/", at + 2);
			if (end < 0)
				return fail(error, Text::tr("The declaration file has an unterminated comment."));
			at = end + 2;
			continue;
		}
		if (c == '{' || c == '}')
		{
			tokens << QString(QLatin1Char(c));
			quotedTokens << false;
			++at;
			continue;
		}
		if (c == '"')
		{
			const auto end = bytes.indexOf('"', at + 1);
			if (end < 0)
				return fail(error, Text::tr("The declaration file has an unterminated quoted name."));
			tokens << QString::fromLatin1(bytes.mid(at + 1, end - at - 1));
			quotedTokens << true;
			at = end + 1;
			continue;
		}
		const auto start = at;
		while (at < bytes.size() && !QByteArrayView(" \t\r\n{}\"").contains(bytes[at]))
			++at;
		tokens << QString::fromLatin1(bytes.mid(start, at - start));
		quotedTokens << false;
	}
	QVector<ModelDoom3Skin> result;
	for (int i = 0; i < tokens.size();)
	{
		const auto isBrace = [&](int index, char brace) { return index < tokens.size() && !quotedTokens[index] && tokens[index] == QLatin1Char(brace); };
		if (i + 2 >= tokens.size() || isBrace(i, '{') || isBrace(i, '}') || !isBrace(i + 2, '{'))
			return fail(error, Text::tr("Expected a declaration type, a name and a brace near token %1.").arg(i + 1));
		const bool skin = tokens[i].compare(QStringLiteral("skin"), Qt::CaseInsensitive) == 0;
		ModelDoom3Skin declaration;
		declaration.name = tokens[i + 1];
		i += 3;
		int depth = 1;
		while (i < tokens.size() && depth > 0)
		{
			if (isBrace(i, '{'))
			{
				if (skin)
					return fail(error, Text::tr("Skin %1 contains a nested brace.").arg(declaration.name));
				++depth;
				++i;
				continue;
			}
			if (isBrace(i, '}'))
			{
				--depth;
				++i;
				continue;
			}
			if (!skin)
			{
				++i;
				continue;
			}
			if (i + 1 >= tokens.size() || isBrace(i + 1, '{') || isBrace(i + 1, '}'))
				return fail(error, Text::tr("Skin %1 has a mapping without a target material.").arg(declaration.name));
			if (!quotedTokens[i] && tokens[i].compare(QStringLiteral("model"), Qt::CaseInsensitive) == 0)
				declaration.models << tokens[i + 1];
			else
				declaration.mappings.append({tokens[i], tokens[i + 1]});
			i += 2;
		}
		if (depth != 0)
			return fail(error, Text::tr("Declaration %1 is not closed.").arg(declaration.name));
		if (skin)
			result.append(std::move(declaration));
	}
	*skins = std::move(result);
	return true;
}
} // namespace vibestudio
