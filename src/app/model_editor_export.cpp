// Native export from the Mesh Editor: the export actions, the MD5 animation
// clip and frame-rate choice, every clip as md5anims, the strict setting, and
// the export report that lists what each format left out or approximated.
#include "app/model_editor_dialog.h"

#include "app/dialog_screen_fit.h"
#include "app/model_editor_tools.h"
#include "app/model_md5_export_dialog.h"
#include "app/wrapping_check_box.h"
#include "core/model_file_io.h"
#include "core/model_lod.h"
#include "core/model_skeletal_export.h"
#include "core/model_targets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelEditor)
};
QString filterFor(const QString &format)
{
	return format == QStringLiteral("mdl")		 ? Text::tr("Quake model (*.mdl)")
		   : format == QStringLiteral("md2")	 ? Text::tr("Quake II model (*.md2)")
		   : format == QStringLiteral("md3")	 ? Text::tr("Quake III model (*.md3)")
		   : format == QStringLiteral("md5mesh") ? Text::tr("Doom 3 MD5 mesh (*.md5mesh)")
		   : format == QStringLiteral("md5anim") ? Text::tr("Doom 3 MD5 animation (*.md5anim)")
		   : format == QStringLiteral("iqm")	 ? Text::tr("Inter-Quake Model (*.iqm)")
		   : format == QStringLiteral("ase")	 ? Text::tr("ASCII scene export (*.ase)")
												 : Text::tr("Wavefront OBJ (*.obj)");
}
// One report for a batch of md5anim files: the notes every clip shares once,
// then each clip's own notes prefixed with its file name.
ModelExportReport combinedClipReport(const QVector<ModelMd5ClipFile> &files)
{
	ModelExportReport combined;
	const auto shared = modelMd5ClipsSharedNotes(files);
	for (const auto &note : shared)
	{
		combined.notes << Text::tr("Every clip: %1").arg(note);
		if (files.first().report.omissions.contains(note))
		{
			combined.omissions << combined.notes.last();
		}
	}
	for (const auto &file : files)
	{
		const auto name = QFileInfo(file.path).fileName();
		for (const auto &note : file.report.notes)
		{
			if (shared.contains(note))
			{
				continue;
			}
			const auto line = Text::tr("%1: %2").arg(name, note);
			combined.notes << line;
			if (file.report.omissions.contains(note))
			{
				combined.omissions << line;
			}
		}
	}
	return combined;
}
} // namespace

void ModelEditorDialog::addExportReportButton(QHBoxLayout *row)
{
	m_exportReport = new QPushButton(Text::tr("Export Report…"));
	m_exportReport->setObjectName(QStringLiteral("meshExportReport"));
	m_exportReport->setAccessibleName(Text::tr("Export report"));
	m_exportReport->setAccessibleDescription(
		Text::tr("List everything the last export or handoff left out or approximated, with the file it wrote."));
	m_exportReport->setToolTip(m_exportReport->accessibleDescription());
	m_exportReport->setAutoDefault(false);
	// Shown once there is a report, so the row stays unchanged until then.
	m_exportReport->hide();
	connect(m_exportReport, &QPushButton::clicked, this, &ModelEditorDialog::showExportReport);
	row->addWidget(m_exportReport);
}

void ModelEditorDialog::addStrictExportControl(QFormLayout *form)
{
	m_strictExport = new WrappingCheckBox(Text::tr("Refuse exports that leave data out"));
	m_strictExport->setObjectName(QStringLiteral("meshStrictExport"));
	m_strictExport->setAccessibleName(m_strictExport->text());
	m_strictExport->setAccessibleDescription(
		Text::tr("Write nothing when the format would leave out data the editable source holds, such as tags, clips, material slots or "
				 "frame edits; Export Report then lists what. Applies to every export, stage and detail set, like model build --strict."));
	m_strictExport->setToolTip(m_strictExport->accessibleDescription());
	form->addRow(m_strictExport);
}

bool ModelEditorDialog::strictExport() const { return m_strictExport && m_strictExport->isChecked(); }

void ModelEditorDialog::presentExportReport(const QString &summary, const ModelExportReport &report, LiveStatus kind)
{
	m_lastExport = report;
	m_lastExportSummary = summary;
	// The summary and the most important note stay in view; every note is one
	// click away.
	QString status = summary;
	if (!report.omissions.isEmpty())
	{
		status += QLatin1Char(' ') + Text::tr("The format leaves out %n kind(s) of data the source holds. %1 Open Export Report for every note.",
											  nullptr, int(report.omissions.size()))
										 .arg(report.omissions.first());
	}
	else if (!report.notes.isEmpty())
	{
		status += QLatin1Char(' ') + Text::tr("%1 Open Export Report for every note.").arg(report.notes.first());
	}
	m_status->setText(status, kind);
	m_exportReport->setVisible(true);
	m_exportReport->setText(report.omissions.isEmpty()
								? Text::tr("Export Report…")
								: Text::tr("Export Report (%n left out)…", nullptr, int(report.omissions.size())));
	m_exportReport->setAccessibleName(m_exportReport->text());
}

void ModelEditorDialog::clearExportReport()
{
	m_lastExport = {};
	m_lastExportSummary.clear();
	if (m_exportReport)
	{
		m_exportReport->hide();
		m_exportReport->setText(Text::tr("Export Report…"));
		m_exportReport->setAccessibleName(Text::tr("Export report"));
	}
}

QString ModelEditorDialog::deliveryTarget(const QString &format) const
{
	const auto id = format.toLower();
	if (m_deliveryTargets.contains(id))
	{
		return m_deliveryTargets.value(id);
	}
	// The project's saved choice, else its installation's suggestion, else the
	// format's default (model_editor_targets.cpp).
	return resolveModelDeliveryTarget(id, {}, m_deliveryContext.project, m_deliveryContext.installation).target;
}

bool ModelEditorDialog::setDeliveryTarget(const QString &format, const QString &id)
{
	const auto key = format.toLower();
	if (!findModelTargetProfile(key, id, nullptr))
	{
		return false;
	}
	m_deliveryTargets.insert(key, id.trimmed().toLower());
	// The live summary in the mesh tools follows the choice.
	if (m_tools)
	{
		m_tools->documentChanged();
	}
	return true;
}

void ModelEditorDialog::presentBlockedExport(const QString &format, const ModelExportReport &report)
{
	m_lastExport = report;
	const int blockers = int(report.target.issues.size());
	m_lastExportSummary = Text::tr("Nothing was written: the model exceeds %n limit(s) of the %1 delivery target %2.", nullptr, blockers)
							  .arg(format.toUpper(), report.target.label);
	m_status->setText(m_lastExportSummary + QLatin1Char(' ') +
						  Text::tr("%1 Open Export Report for every limit and its fix.").arg(report.target.issues.first().message),
					  LiveStatus::Problem);
	m_exportReport->setVisible(true);
	m_exportReport->setText(Text::tr("Export Report (%n blocker(s))…", nullptr, blockers));
	m_exportReport->setAccessibleName(m_exportReport->text());
}

void ModelEditorDialog::presentRefusedExport(const QString &format, const ModelExportReport &report, const QString &error)
{
	if (!report.target.issues.isEmpty())
	{
		presentBlockedExport(format, report);
	}
	else if (!report.omissions.isEmpty())
	{
		// A strict refusal keeps the report of what the export would leave out.
		presentExportReport(Text::tr("Nothing was written: %1 would leave out data the editable source holds, and Refuse exports that leave "
									 "data out is on.")
								.arg(format.toUpper()),
							report, LiveStatus::Problem);
	}
	else
	{
		m_status->setText(error, LiveStatus::Problem);
	}
}

bool ModelEditorDialog::checkDeliveryTargets(QVector<ModelTargetReport> *reports, bool show, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	error->clear();
	if (m_working)
	{
		return false;
	}
	QVector<ModelTargetProfile> profiles;
	for (const auto &format : {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("iqm"), QStringLiteral("md5mesh"),
							   QStringLiteral("md5anim")})
	{
		ModelTargetProfile profile;
		// IQM and the MD5 files are listed for skeletal models, as the header
		// summary does.
		const bool skeletal = format == QStringLiteral("iqm") || format.startsWith(QStringLiteral("md5"));
		if ((!skeletal || !m_document.mesh().skeleton.isEmpty()) && findModelTargetProfile(format, deliveryTarget(format), &profile))
		{
			profiles.append(profile);
		}
	}
	QVector<ModelTargetReport> results;
	if (!performWork(
			Text::tr("Check Delivery Targets"),
			[profiles, &results](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				for (const auto &profile : profiles)
				{
					ModelTargetReport report;
					if (!assessModelTarget(candidate.mesh(), profile, &report, failure, control))
					{
						return false;
					}
					results.append(std::move(report));
				}
				return true;
			},
			error))
	{
		m_status->setText(*error);
		return false;
	}
	int blocked = 0;
	QStringList lines;
	for (const auto &report : std::as_const(results))
	{
		blocked += !report.ready();
		const int issues = int(report.issues.size());
		lines << (report.ready() ? Text::tr("%1 · %2: ready").arg(report.format.toUpper(), report.label)
								 : Text::tr("%1 · %2: %n blocker(s)", nullptr, issues).arg(report.format.toUpper(), report.label));
		for (const auto &line : modelTargetIssueLines(report))
		{
			lines << QStringLiteral("  • ") + line;
		}
		// The same measure wording as the header menu, upper bounds included.
		for (const auto &line : modelTargetMeasureLines(report))
		{
			lines << QStringLiteral("    ") + line;
		}
		lines << QString();
	}
	const int checked = int(results.size());
	m_status->setText(blocked == 0 ? Text::tr("The model is ready for the %Ln selected delivery target(s).", nullptr, checked)
								   : Text::tr("Selected delivery targets that report blockers: %Ln. Open the check for each limit and its fix.", nullptr,
											  blocked),
					  LiveStatus::Result);
	if (reports)
	{
		*reports = results;
	}
	if (show)
	{
		QDialog dialog(this);
		dialog.setObjectName(QStringLiteral("meshDeliveryTargetsDialog"));
		dialog.setWindowTitle(Text::tr("Delivery Targets"));
		dialog.setAccessibleName(dialog.windowTitle());
		dialog.setAccessibleDescription(Text::tr("Every limit of the selected delivery targets, with the model's values and a fix for each blocker."));
		auto *layout = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(lines.join(QLatin1Char('\n')));
		text->setObjectName(QStringLiteral("meshDeliveryTargetsText"));
		text->setReadOnly(true);
		text->setAccessibleName(Text::tr("Delivery target details"));
		layout->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		fitDialogToScreen(&dialog, QSize(760, 460));
		text->setFocus(Qt::OtherFocusReason);
		dialog.exec();
	}
	return true;
}

bool ModelEditorDialog::selectTargetIssue(const ModelTargetIssue &issue)
{
	const auto &mesh = m_document.mesh();
	if (m_working || issue.surface < 0 || issue.surface >= mesh.surfaces.size())
	{
		return false;
	}
	// The faces or vertices the issue names, in their mode; else surface mode
	// shows the whole surface the limit is about.
	const auto &surface = mesh.surfaces.at(issue.surface);
	const bool faces = !issue.faces.isEmpty() && std::all_of(issue.faces.cbegin(), issue.faces.cend(), [&](int f) { return f >= 0 && f < surface.triangles.size(); });
	const bool vertices = !faces && !issue.vertices.isEmpty() &&
						  std::all_of(issue.vertices.cbegin(), issue.vertices.cend(), [&](int v) { return v >= 0 && v < surface.vertexCount; });
	ModelSelection selected;
	selected.surface = issue.surface;
	if (faces)
		selected.faces = QSet<int>(issue.faces.cbegin(), issue.faces.cend());
	else if (vertices)
		selected.vertices = QSet<int>(issue.vertices.cbegin(), issue.vertices.cend());
	else
		selected.surfaces.insert(issue.surface);
	{
		QSignalBlocker blocker(m_selectionMode);
		m_selectionMode->setCurrentIndex(faces ? 0 : vertices ? 1 : 5);
	}
	m_document.setSelection(selected);
	refresh();
	m_status->setText(faces ? Text::tr("Selected %n face(s) of surface \"%1\": %2", nullptr, int(issue.faces.size())).arg(surface.name, issue.message)
					  : vertices ? Text::tr("Selected %n vertex(es) of surface \"%1\": %2", nullptr, int(issue.vertices.size())).arg(surface.name, issue.message)
								 : Text::tr("Selected surface \"%1\": %2").arg(surface.name, issue.message));
	return true;
}

void ModelEditorDialog::showExportReport()
{
	QStringList lines{m_lastExportSummary};
	if (!m_lastExport.target.target.isEmpty())
	{
		lines << Text::tr("Delivery target: %1 [%2]").arg(m_lastExport.target.label, m_lastExport.target.target);
	}
	if (!m_lastExport.target.issues.isEmpty())
	{
		lines << QString() << Text::tr("Limits exceeded, with a fix for each:");
		for (const auto &line : modelTargetIssueLines(m_lastExport.target))
		{
			lines << QStringLiteral("• ") + line;
		}
	}
	if (!m_lastExport.omissions.isEmpty())
	{
		lines << QString() << Text::tr("Left out or changed:");
		for (const auto &omission : std::as_const(m_lastExport.omissions))
		{
			lines << QStringLiteral("• ") + omission;
		}
	}
	QStringList others;
	for (const auto &note : std::as_const(m_lastExport.notes))
	{
		if (!m_lastExport.omissions.contains(note))
		{
			others << QStringLiteral("• ") + note;
		}
	}
	if (!others.isEmpty())
	{
		lines << QString() << Text::tr("Notes:") << others;
	}
	if (m_lastExport.notes.isEmpty() && m_lastExport.target.issues.isEmpty())
	{
		lines << QString() << Text::tr("The format holds everything this export wrote; there are no notes.");
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("meshExportReportDialog"));
	dialog.setWindowTitle(Text::tr("Export Report"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto *layout = new QVBoxLayout(&dialog);
	auto *text = new QPlainTextEdit(lines.join(QLatin1Char('\n')));
	text->setObjectName(QStringLiteral("meshExportReportText"));
	text->setReadOnly(true);
	text->setAccessibleName(Text::tr("Export report details"));
	layout->addWidget(text);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	fitDialogToScreen(&dialog, QSize(760, 420));
	text->setFocus(Qt::OtherFocusReason);
	dialog.exec();
}

bool ModelEditorDialog::exportToPath(const QString &path, const QString &format, const ModelExportOptions &options, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	error->clear();
	if (m_working || path.isEmpty())
	{
		return false;
	}
	qint64 written = 0;
	ModelExportReport report;
	ModelLodSiblingReview siblings;
	if (!performWork(
			Text::tr("Export Model"),
			[path, format, options, &written, &report, &siblings](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				// Review the destination before preparation so a competing write during
				// export cannot be silently accepted as the file the user approved.
				const auto target = inspectModelWriteTarget(path, control);
				if (!target.isValid())
				{
					*failure = target.error;
					return false;
				}
				if (modelPathsReferToSameFile(path, candidate.path()))
				{
					*failure = Text::tr("Choose an export path separate from the editable source.");
					return false;
				}
				const auto bytes = exportEditableModel(candidate.mesh(), format, options, failure, control, &report);
				if (bytes.isEmpty())
				{
					return false;
				}
				// An MD3 replaces only its own file: detail levels beside it still
				// load with it, and stored levels are written as a set. Reviewed
				// before writing, so a cancelled review writes nothing.
				if (format == QStringLiteral("md3"))
				{
					const auto &mesh = candidate.mesh();
					if (!mesh.detailLevels.isEmpty())
					{
						report.notes << Text::tr("The source stores %n detail level(s); Export writes the base model only. Write the set with Detail "
												 "Levels… > Export… or Export Quake III Detail Levels….",
												 nullptr, int(mesh.detailLevels.size()));
					}
					ModelTargetProfile profile;
					if (findModelTargetProfile(format, options.target, &profile) &&
						!reviewModelLodSiblings(mesh, path, 0, modelLodDiskReader(), profile, &siblings, failure, control))
					{
						return false;
					}
					report.notes += modelLodSiblingLines(siblings);
				}
				if (!writeModelFile(target, bytes, failure, control))
				{
					return false;
				}
				written = bytes.size();
				return true;
			},
			error, true))
	{
		presentRefusedExport(format, report, *error);
		return false;
	}
	presentExportReport(Text::tr("Exported %1 to %2. The editable source is saved separately.").arg(QLocale().formattedDataSize(written), path) +
							(siblings.isEmpty() ? QString() : QLatin1Char(' ') + modelLodSiblingSummary(siblings)) + nativeUvStatus(),
						report);
	return true;
}

bool ModelEditorDialog::exportMd5Clips(const QString &directory, const ModelExportOptions &options, bool overwrite, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	error->clear();
	if (m_working || directory.isEmpty())
	{
		return false;
	}
	QVector<ModelMd5ClipFile> files;
	if (!performWork(
			Text::tr("Export MD5 Animations"),
			[directory, options, overwrite, &files](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				return prepareModelMd5Clips(candidate.mesh(), directory, options, &files, failure, control) &&
					   writeModelMd5Clips(files, overwrite, false, candidate.path(), failure, control);
			},
			error, true))
	{
		// A strict refusal prepared every clip, so the report lists each one's
		// omissions.
		const bool refused = options.strict && std::any_of(files.cbegin(), files.cend(), [](const ModelMd5ClipFile &file) { return file.bytes.isEmpty(); });
		if (refused)
		{
			presentExportReport(Text::tr("Nothing was written: the md5anim files would leave out data the editable source holds, and Refuse "
										 "exports that leave data out is on."),
								combinedClipReport(files), LiveStatus::Problem);
		}
		else
		{
			m_status->setText(*error);
		}
		return false;
	}
	qint64 bytes = 0;
	for (const auto &file : std::as_const(files))
	{
		bytes += file.bytes.size();
	}
	presentExportReport(Text::tr("Exported %Ln md5anim file(s), %1 in total, to %2.", nullptr, int(files.size()))
							.arg(QLocale().formattedDataSize(bytes), directory),
						combinedClipReport(files));
	return true;
}

void ModelEditorDialog::exportModel(const QString &format)
{
	if (m_working)
	{
		return;
	}
	if (format == QStringLiteral("md5anim"))
	{
		exportMd5Animation(false);
		return;
	}
	const auto path = QFileDialog::getSaveFileName(this, Text::tr("Export Model"), {}, filterFor(format));
	if (path.isEmpty())
	{
		return;
	}
	ModelExportOptions options;
	options.frame = m_frame->currentIndex();
	options.target = deliveryTarget(format);
	options.strict = strictExport();
	exportToPath(path, format, options);
}

bool ModelEditorDialog::chooseMd5Animation(bool allClips, const QString &preferredClip, ModelExportOptions *options)
{
	const auto &mesh = m_document.mesh();
	if (mesh.skeleton.isEmpty() || mesh.skeleton.clips.isEmpty())
	{
		m_status->setText(Text::tr("An md5anim needs a skeletal clip; this model animates by frames. Add a clip with Edit Skeletal Animation."),
						  LiveStatus::Problem);
		return false;
	}
	// Start from the clip named, else the one the current frame belongs to, as
	// the preview shows it.
	int current = modelSkeletalClipIndex(mesh.skeleton, preferredClip);
	if (current < 0)
	{
		current = std::max(0, modelSkeletalClipForFrame(mesh, m_frame->currentIndex()));
	}
	const auto revision = m_document.revisionFingerprint();
	ModelMd5ExportDialog chooser(mesh, current, allClips, this);
	if (chooser.exec() != QDialog::Accepted)
	{
		return false;
	}
	if (revision != m_document.revisionFingerprint())
	{
		m_status->setText(Text::tr("The model changed while choosing the MD5 animation. Export again."), LiveStatus::Problem);
		return false;
	}
	const auto chosen = chooser.options();
	options->clip = chosen.clip;
	options->md5FrameRate = chosen.md5FrameRate;
	return true;
}

void ModelEditorDialog::exportMd5Animation(bool allClips)
{
	if (m_working)
	{
		return;
	}
	ModelExportOptions options;
	if (!chooseMd5Animation(allClips, {}, &options))
	{
		return;
	}
	options.target = deliveryTarget(QStringLiteral("md5anim"));
	options.strict = strictExport();
	if (!allClips)
	{
		const auto suggested = modelMd5ClipFileName(m_document.mesh().skeleton, options.clip);
		const auto path = QFileDialog::getSaveFileName(this, Text::tr("Export MD5 Animation"), suggested, filterFor(QStringLiteral("md5anim")));
		if (!path.isEmpty())
		{
			exportToPath(path, QStringLiteral("md5anim"), options);
		}
		return;
	}
	const auto directory = QFileDialog::getExistingDirectory(this, Text::tr("Export MD5 Animations of All Clips"));
	if (directory.isEmpty())
	{
		return;
	}
	QStringList existing;
	for (int clip = 0; clip < m_document.mesh().skeleton.clips.size(); ++clip)
	{
		const auto name = modelMd5ClipFileName(m_document.mesh().skeleton, clip);
		if (QFileInfo::exists(QDir(directory).filePath(name)))
		{
			existing << name;
		}
	}
	bool overwrite = false;
	if (!existing.isEmpty())
	{
		const auto answer = QMessageBox::question(
			this, Text::tr("Replace MD5 Animations"),
			Text::tr("Files in this folder that would be replaced (%n): %1. Replace them?", nullptr, int(existing.size()))
				.arg(existing.join(QStringLiteral(", "))),
			QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer != QMessageBox::Yes)
		{
			return;
		}
		overwrite = true;
	}
	exportMd5Clips(directory, options, overwrite);
}
} // namespace vibestudio
