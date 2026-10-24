// Delivery targets in the Mesh Editor beyond the preflight: Prepare for
// Target (core/model_target_prepare.h) with its review, and the targets and
// strict setting saved with the open project (core/model_delivery.h).
#include "app/model_editor_dialog.h"

#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "core/model_target_prepare.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelEditor)
};

// The fixes Prepare for Target can make for a format, in review order.
QVector<ModelTargetFixKind> fixKindsFor(const QString &format)
{
	if (format == QStringLiteral("mdl") || format == QStringLiteral("md2"))
		return {ModelTargetFixKind::Join, ModelTargetFixKind::Decimate};
	if (format == QStringLiteral("md3"))
		return {ModelTargetFixKind::Split, ModelTargetFixKind::Decimate, ModelTargetFixKind::Fit};
	if (format == QStringLiteral("iqm") || format == QStringLiteral("md5mesh"))
		return {ModelTargetFixKind::Split, ModelTargetFixKind::Decimate, ModelTargetFixKind::Influences};
	return {};
}

bool allowed(const ModelTargetPrepareOptions &options, ModelTargetFixKind kind)
{
	switch (kind)
	{
	case ModelTargetFixKind::Split:
		return options.split;
	case ModelTargetFixKind::Join:
		return options.join;
	case ModelTargetFixKind::Decimate:
		return options.decimate;
	case ModelTargetFixKind::Fit:
		return options.fit;
	case ModelTargetFixKind::Influences:
		return options.influences;
	}
	return false;
}

void allow(ModelTargetPrepareOptions *options, ModelTargetFixKind kind, bool on)
{
	switch (kind)
	{
	case ModelTargetFixKind::Split:
		options->split = on;
		break;
	case ModelTargetFixKind::Join:
		options->join = on;
		break;
	case ModelTargetFixKind::Decimate:
		options->decimate = on;
		break;
	case ModelTargetFixKind::Fit:
		options->fit = on;
		break;
	case ModelTargetFixKind::Influences:
		options->influences = on;
		break;
	}
}

QString fixKindDescription(ModelTargetFixKind kind)
{
	switch (kind)
	{
	case ModelTargetFixKind::Split:
		return Text::tr("Separate a surface over its vertex or triangle budget into connected surfaces that fit, keeping every vertex and pose.");
	case ModelTargetFixKind::Join:
		return Text::tr("Join every surface into the first, which MDL and MD2 need; surfaces with other materials take the first surface's.");
	case ModelTargetFixKind::Decimate:
		return Text::tr("Reduce what splitting cannot fit with a quadric decimation to the target's budget; this removes detail.");
	case ModelTargetFixKind::Fit:
		return Text::tr("Move the model, and scale it down only if it is too large, so every pose fits MD3's -512 to 511.98 unit range.");
	case ModelTargetFixKind::Influences:
		return Text::tr("Keep the strongest joint influences of each vertex that the format stores, renormalised.");
	}
	return {};
}
} // namespace

ModelDeliveryOrigin ModelEditorDialog::deliveryTargetOrigin(const QString &format) const
{
	const auto id = format.toLower();
	if (m_deliveryTargets.contains(id))
		return ModelDeliveryOrigin::Explicit;
	return resolveModelDeliveryTarget(id, {}, m_deliveryContext.project, m_deliveryContext.installation).origin;
}

void ModelEditorDialog::syncDeliveryContext()
{
	const auto current = deliveryContext ? deliveryContext() : ModelEditorDeliveryContext{};
	const auto key = current.projectRoot + QLatin1Char('|') +
					 QString::fromUtf8(QJsonDocument(modelDeliveryPreferencesJson(current.project)).toJson(QJsonDocument::Compact)) +
					 QLatin1Char('|') + QString::fromUtf8(QJsonDocument(modelDeliveryPreferencesJson(current.installation)).toJson(QJsonDocument::Compact));
	if (key == m_deliveryContextKey)
		return;
	const bool newProject = m_deliveryContextKey.isEmpty() || current.projectRoot != m_deliveryContext.projectRoot;
	m_deliveryContext = current;
	m_deliveryContextKey = key;
	// A project's saved strict setting applies when the project opens; later
	// changes on the Export page last for the session.
	if (newProject && current.project.strictSet && m_strictExport)
		m_strictExport->setChecked(current.project.strict);
	if (m_tools)
		m_tools->documentChanged();
}

bool ModelEditorDialog::saveDeliveryTargetsToProject(QString *error)
{
	QString local;
	if (!error)
		error = &local;
	error->clear();
	syncDeliveryContext();
	if (!saveDeliveryPreferences || m_deliveryContext.projectRoot.isEmpty())
	{
		*error = Text::tr("Open a project to save delivery targets with it.");
		m_status->setText(*error);
		return false;
	}
	auto preferences = m_deliveryContext.project;
	for (auto it = m_deliveryTargets.cbegin(); it != m_deliveryTargets.cend(); ++it)
		preferences.targets.insert(it.key(), it.value());
	preferences.strictSet = true;
	preferences.strict = strictExport();
	if (!saveDeliveryPreferences(preferences, error))
	{
		m_status->setText(*error);
		return false;
	}
	// The choices now come from the project.
	m_deliveryContext.project = preferences;
	m_deliveryTargets.clear();
	m_deliveryContextKey.clear();
	syncDeliveryContext();
	const int saved = int(preferences.targets.size());
	m_status->setText(Text::tr("Saved %n delivery target(s) and the strict-export setting with the project %1.", nullptr, saved).arg(m_deliveryContext.projectRoot));
	if (m_tools)
		m_tools->documentChanged();
	return true;
}

void ModelEditorDialog::previewTransientMesh(const ModelMesh *mesh)
{
	if (mesh)
	{
		m_preview->setMesh(*mesh, true);
		// Any later refresh shows the document again.
		m_presentedMeshRevision = QByteArrayLiteral("transient-preview");
		return;
	}
	m_presentedMeshRevision.clear();
	refresh();
}

bool ModelEditorDialog::prepareForTarget(const QString &format, bool review, QString *error)
{
	QString local;
	if (!error)
		error = &local;
	error->clear();
	if (m_working)
		return false;
	ModelTargetProfile profile;
	if (!findModelTargetProfile(format, deliveryTarget(format), &profile))
	{
		*error = Text::tr("%1 has no delivery targets.").arg(format.toUpper());
		m_status->setText(*error);
		return false;
	}
	const auto base = m_document.revisionFingerprint();
	ModelTargetPreparation preparation;
	const auto run = [this, &profile, &preparation, error](const ModelTargetPrepareOptions &options)
	{
		ModelTargetPreparation result;
		if (!performWork(
				Text::tr("Prepare for Target"),
				[&profile, options, &result](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
				{ return prepareModelForTarget(candidate.mesh(), profile, options, &result, failure, control); },
				error))
			return false;
		preparation = std::move(result);
		return true;
	};
	ModelTargetPrepareOptions options;
	if (!run(options))
	{
		m_status->setText(*error);
		return false;
	}
	if (review)
	{
		QDialog dialog(this);
		dialog.setObjectName(QStringLiteral("meshPrepareTargetDialog"));
		dialog.setWindowTitle(Text::tr("Prepare for Target"));
		dialog.setAccessibleName(dialog.windowTitle());
		dialog.setAccessibleDescription(Text::tr("Review the changes that prepare the model for its delivery target, choose which kinds to make, and apply them as one undo step."));
		auto *layout = new QVBoxLayout(&dialog);
		auto *heading = new QLabel(Text::tr("%1 · %2").arg(profile.format.toUpper(), profile.label));
		heading->setObjectName(QStringLiteral("meshPrepareTargetHeading"));
		heading->setWordWrap(true);
		layout->addWidget(heading);
		QVector<QPair<ModelTargetFixKind, QCheckBox *>> kinds;
		for (auto kind : fixKindsFor(profile.format))
		{
			auto *box = new QCheckBox(modelTargetFixKindName(kind));
			box->setObjectName(QStringLiteral("meshPrepareFix-%1").arg(modelTargetFixKindId(kind)));
			box->setChecked(allowed(options, kind));
			box->setToolTip(fixKindDescription(kind));
			box->setAccessibleName(box->text());
			box->setAccessibleDescription(box->toolTip());
			layout->addWidget(box);
			kinds.append({kind, box});
		}
		auto *text = new QPlainTextEdit;
		text->setObjectName(QStringLiteral("meshPrepareTargetReview"));
		text->setReadOnly(true);
		text->setAccessibleName(Text::tr("Prepared changes"));
		text->setAccessibleDescription(Text::tr("Every change the preparation makes, what to know about it, and the limits that still block the target."));
		layout->addWidget(text, 1);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
		auto *apply = buttons->addButton(Text::tr("Apply Changes"), QDialogButtonBox::AcceptRole);
		apply->setObjectName(QStringLiteral("meshPrepareTargetApply"));
		apply->setAccessibleName(apply->text());
		apply->setAccessibleDescription(Text::tr("Applies every listed change to the model as one undo step."));
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		const auto show = [&]
		{
			text->setPlainText(modelTargetPreparationLines(preparation).join(QLatin1Char('\n')));
			apply->setEnabled(preparation.changed());
		};
		show();
		for (const auto &[kind, box] : std::as_const(kinds))
		{
			const auto changed = [&, kind = kind](bool on)
			{
				allow(&options, kind, on);
				if (!run(options))
				{
					text->setPlainText(*error);
					apply->setEnabled(false);
					return;
				}
				show();
			};
			connect(box, &QCheckBox::toggled, &dialog, changed);
		}
		dialog.resize(720, 460);
		(kinds.isEmpty() ? static_cast<QWidget *>(text) : kinds.first().second)->setFocus(Qt::OtherFocusReason);
		if (dialog.exec() != QDialog::Accepted)
		{
			m_status->setText(Text::tr("Prepare for Target was cancelled; the model is unchanged."));
			return false;
		}
	}
	if (!preparation.changed())
	{
		const int left = int(preparation.after.issues.size());
		m_status->setText(left == 0 ? Text::tr("The model is ready for %1; nothing to change.").arg(profile.label)
									: Text::tr("Prepare for Target cannot fix the remaining %n limit(s) of %1; Check Delivery Targets lists each with its fix.",
											   nullptr, left)
										  .arg(profile.label));
		return true;
	}
	const auto prepared = preparation.mesh;
	const auto selection = preparation.selection;
	if (!performWork(
			Text::tr("Apply Target Preparation"),
			[prepared, selection, base](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{ return candidate.adopt(prepared, selection, base, failure, control); },
			error))
	{
		m_status->setText(*error);
		return false;
	}
	// Surfaces may have changed: the material previews follow them again.
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	m_lastSkinBindings.clear();
	m_skinBindingsDetails->setEnabled(false);
	// The changed surfaces are selected; surface mode shows them.
	if (!m_document.selection().surfaces.isEmpty())
	{
		QSignalBlocker blocker(m_selectionMode);
		m_selectionMode->setCurrentIndex(5);
	}
	refresh();
	const int fixes = int(preparation.fixes.size()), left = int(preparation.after.issues.size());
	m_status->setText(left == 0 ? Text::tr("Prepared for %1 with %n change(s), one undo step. The model is ready for it.", nullptr, fixes).arg(profile.label)
								: Text::tr("Prepared for %1 with %n change(s), one undo step. Check Delivery Targets lists what still blocks it.", nullptr, fixes)
									  .arg(profile.label));
	return true;
}
} // namespace vibestudio
