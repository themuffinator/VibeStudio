#include "core/model_skin_bindings.h"

#include "core/package_staging.h"
#include "core/model_archive.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <QFileInfo>

// Format/matching references: id Software's Quake III Arena renderer,
// tr_image.c (CommaParse/RE_RegisterSkin), tr_model.c and tr_mesh.c;
// GPL-2.0-or-later, reviewed 2026-10-05. Original implementation, no copied code.
// https://github.com/id-Software/Quake-III-Arena/tree/master/code/renderer
// See docs/CREDITS.md, Model Skin Bindings.
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelSkinBindings)
};
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
struct Token
{
	enum Kind
	{
		End,
		Word,
		Comma
	} kind = End;
	QString text;
	int line = 1;
};
class Reader
{
  public:
	Reader(const QByteArray &bytes, QString *error, const ModelWorkControl &control)
		: m_bytes(bytes), m_error(error), m_work(control, ModelWorkPhase::Reading, error)
	{
		if (bytes.startsWith("\xEF\xBB\xBF"))
		{
			m_at = 3;
		}
	}
	bool next(Token *token)
	{
		while (m_at < m_bytes.size())
		{
			if (!m_work.step())
			{
				return false;
			}
			const auto c = m_bytes[m_at];
			if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
			{
				m_line += c == '\n';
				++m_at;
				continue;
			}
			if (m_bytes.mid(m_at, 2) == "//")
			{
				while (m_at < m_bytes.size() && m_bytes[m_at] != '\n')
				{
					if (!m_work.step() || !character(m_bytes[m_at++]))
					{
						return false;
					}
				}
				continue;
			}
			if (m_bytes.mid(m_at, 2) == "/*")
			{
				const int start = m_line;
				m_at += 2;
				while (m_at < m_bytes.size() && m_bytes.mid(m_at, 2) != "*/")
				{
					const auto part = m_bytes[m_at++];
					if (!m_work.step() || !character(part))
					{
						return false;
					}
					m_line += part == '\n';
				}
				if (m_at == m_bytes.size())
				{
					return lineError(start, Text::tr("Unterminated block comment."));
				}
				m_at += 2;
				continue;
			}
			break;
		}
		*token = {};
		token->line = m_line;
		if (m_at == m_bytes.size())
		{
			return m_work.check();
		}
		if (m_bytes[m_at] == ',')
		{
			++m_at;
			token->kind = Token::Comma;
			return true;
		}
		const bool quoted = m_bytes[m_at] == '"';
		if (quoted)
		{
			++m_at;
		}
		QByteArray value;
		while (m_at < m_bytes.size())
		{
			const auto c = m_bytes[m_at];
			if (!m_work.step() || !character(c))
			{
				return false;
			}
			if (quoted && c == '"')
			{
				++m_at;
				break;
			}
			if (!quoted && (c <= ' ' || c == ','))
			{
				break;
			}
			if (c == '\n' || c == '\r' || c == '\t' || c == '"' || c == ',')
			{
				return lineError(token->line, Text::tr("Names cannot contain quotes, commas or control characters."));
			}
			value += c;
			++m_at;
			if (value.size() > 63)
			{
				return lineError(token->line, Text::tr("A skin name or shader path exceeds 63 ASCII bytes."));
			}
		}
		if (quoted && (m_at == 0 || m_bytes[m_at - 1] != '"'))
		{
			return lineError(token->line, Text::tr("Unterminated quoted name."));
		}
		if (value.isEmpty())
		{
			return lineError(token->line, Text::tr("Expected a nonempty surface or shader name."));
		}
		token->kind = Token::Word;
		token->text = QString::fromLatin1(value);
		return true;
	}
	bool lineError(int line, const QString &message)
	{
		return fail(m_error, Text::tr("Skin line %1: %2").arg(line).arg(message));
	}

  private:
	const QByteArray &m_bytes;
	QString *m_error;
	ModelWorkProgress m_work;
	qsizetype m_at = 0;
	int m_line = 1;
	bool character(char c)
	{
		return (uchar(c) >= 32 && uchar(c) < 127) || c == '\n' || c == '\r' || c == '\t' ||
			   lineError(m_line, Text::tr("Skin files require ASCII text without binary or control characters."));
	}
};
} // namespace

bool planModelSkinBindings(const ModelMesh &mesh, const QByteArray &bytes, ModelSkinBindingPlan *plan, QString *error,
						   const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, bytes.size(), error))
	{
		return false;
	}
	if (!plan || bytes.isEmpty() || bytes.size() > modelSkinBindingByteLimit)
	{
		return fail(error, Text::tr("Choose a skin file containing between 1 byte and 64 KiB."));
	}
	if (!mesh.geometryAvailable || mesh.surfaces.isEmpty() || mesh.surfaces.size() > 32 || mesh.mdl.enabled ||
		!mesh.embeddedSkins.isEmpty())
	{
		return fail(error, Text::tr("Skin shader assignments require an editable model without embedded MDL skins."));
	}
	Reader reader(bytes, error, control);
	QMap<QString, QString> bindings;
	ModelSkinBindingPlan candidate;
	int count = 0;
	for (;;)
	{
		Token surface, comma, shader;
		if (!reader.next(&surface))
		{
			return false;
		}
		if (surface.kind == Token::End)
		{
			break;
		}
		if (++count > 256)
		{
			return reader.lineError(surface.line, Text::tr("A skin file may contain at most 256 records."));
		}
		if (!reader.next(&comma))
		{
			return false;
		}
		if (surface.kind != Token::Word || comma.kind != Token::Comma)
		{
			return reader.lineError(surface.line, Text::tr("Expected surface,shader or an empty tag_ marker."));
		}
		const auto name = surface.text.toLower();
		if (surface.text.contains(QStringLiteral("tag_")))
		{
			candidate.ignoredTags.append(surface.text);
			continue;
		}
		if (bindings.contains(name))
		{
			return reader.lineError(surface.line, Text::tr("Duplicate surface assignment: %1.").arg(surface.text));
		}
		if (!reader.next(&shader))
		{
			return false;
		}
		if (shader.kind != Token::Word || !isSafePackageVirtualPath(shader.text) || shader.text.contains('\\'))
		{
			return reader.lineError(surface.line, Text::tr("Choose a safe package-relative shader path using forward slashes."));
		}
		bindings.insert(name, shader.text);
	}
	QSet<QString> used;
	for (int index = 0; index < mesh.surfaces.size(); ++index)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, index, mesh.surfaces.size(), error))
		{
			return false;
		}
		const auto &surface = mesh.surfaces[index];
		auto name = surface.name.toLower();
		// Original Quake III strips any final underscore plus one character.
		if (name.size() > 2 && name[name.size() - 2] == '_')
		{
			name.chop(2);
		}
		const auto found = bindings.constFind(name);
		if (found == bindings.cend())
		{
			return fail(
				error,
				Text::tr("Skin has no assignment for surface %1 (engine name %2). Every surface must be covered.").arg(surface.name, name));
		}
		used.insert(name);
		candidate.assignments.append({index, surface.name, name, surface.skinPaths.value(0), found.value()});
	}
	for (auto it = bindings.cbegin(); it != bindings.cend(); ++it)
	{
		if (!used.contains(it.key()))
		{
			candidate.unusedSurfaces.append(it.key());
		}
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, mesh.surfaces.size(), mesh.surfaces.size(), error))
	{
		return false;
	}
	*plan = std::move(candidate);
	return true;
}

bool readModelSkinBindings(const QString &path, ModelSkinBindingInput *input, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	const QFileInfo file(path);
	if (!input || !path.endsWith(QStringLiteral(".skin"), Qt::CaseInsensitive) || !file.isFile() || file.size() <= 0 ||
		file.size() > modelSkinBindingByteLimit)
	{
		return fail(error, Text::tr("Choose a readable .skin file containing between 1 byte and 64 KiB."));
	}
	ModelSkinBindingInput candidate;
	candidate.path = file.absoluteFilePath();
	if (!readModelFile(path, &candidate.bytes, error, control))
	{
		return false;
	}
	if (candidate.bytes.size() > modelSkinBindingByteLimit)
	{
		return fail(error, Text::tr("The skin file changed or exceeds 64 KiB."));
	}
	*input = std::move(candidate);
	return true;
}
bool readModelSkinBindings(const PackageArchiveReader &archive, const ModelSkinSourceReference &reference, ModelSkinBindingInput *input,
						   QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Reading, error);
	if (!work.check())
	{
		return false;
	}
	if (!input || !archive.isOpen() || reference.entryIndex < -1)
	{
		return fail(error, Text::tr("Choose a .skin entry from an open package snapshot."));
	}
	const auto entries = archive.entries();
	auto selected = reference.entryIndex;
	if (selected < 0)
	{
		for (qsizetype index = 0; index < entries.size(); ++index)
		{
			if (!work.step())
			{
				return false;
			}
			if (entries[index].kind != PackageEntryKind::File ||
				entries[index].virtualPath.compare(reference.path, Qt::CaseInsensitive) != 0)
			{
				continue;
			}
			if (selected >= 0)
			{
				return fail(error, Text::tr("Repeated skin paths require an exact package entry index."));
			}
			selected = index;
		}
	}
	if (selected < 0 || selected >= entries.size() ||
		!entries[selected].virtualPath.endsWith(QStringLiteral(".skin"), Qt::CaseInsensitive) ||
		(!reference.path.isEmpty() && entries[selected].virtualPath.compare(reference.path, Qt::CaseInsensitive) != 0))
	{
		return fail(error, Text::tr("The selected skin entry is missing or no longer matches its path and index."));
	}
	ModelSkinBindingInput candidate;
	candidate.path = entries[selected].virtualPath;
	candidate.entryIndex = selected;
	ModelArchiveReader reader(archive, control);
	if (!reader.readEntryAt(selected, &candidate.bytes, error, modelSkinBindingByteLimit))
	{
		return false;
	}
	*input = std::move(candidate);
	return true;
}

QJsonObject modelSkinBindingPlanJson(const ModelSkinBindingPlan &plan)
{
	QJsonArray assignments;
	for (const auto &item : plan.assignments)
	{
		assignments.append(QJsonObject{{"surface", item.surface},
									   {"name", item.name},
									   {"engineName", item.engineName},
									   {"previousMaterial", item.previousMaterial},
									   {"material", item.material}});
	}
	return {{"assignments", assignments},
			{"ignoredTags", QJsonArray::fromStringList(plan.ignoredTags)},
			{"unusedSurfaces", QJsonArray::fromStringList(plan.unusedSurfaces)}};
}
QStringList modelSkinBindingPlanText(const ModelSkinBindingPlan &plan)
{
	QStringList lines;
	for (const auto &item : plan.assignments)
	{
		lines << Text::tr("%1: %2 → %3").arg(item.name, item.previousMaterial, item.material);
	}
	if (!plan.ignoredTags.isEmpty())
	{
		lines << Text::tr("Attachment markers ignored: %1.").arg(plan.ignoredTags.join(QStringLiteral(", ")));
	}
	if (!plan.unusedSurfaces.isEmpty())
	{
		lines << Text::tr("Bindings for other surfaces were not used: %1.").arg(plan.unusedSurfaces.join(QStringLiteral(", ")));
	}
	return lines;
}
} // namespace vibestudio
