#include "app/model_editor_dialog.h"
#include "app/model_uv_view.h"
#include "app/model_viewport.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace vibestudio
{
void ModelEditorDialog::addMdlControls(QFormLayout *page)
{
	const auto group = [&](const QString &title)
	{
		auto *box = new QGroupBox(title);
		auto *form = new QFormLayout(box);
		form->setContentsMargins(4, 6, 4, 6);
		form->setRowWrapPolicy(QFormLayout::WrapAllRows);
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		page->addRow(box);
		return form;
	};
	const auto field = [](QFormLayout *form, QWidget *widget, const char *id, const QString &name, const QString &description)
	{
		widget->setObjectName(QString::fromLatin1(id));
		widget->setAccessibleName(name);
		widget->setAccessibleDescription(description);
		widget->setToolTip(description);
		widget->setMinimumWidth(0);
		widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		form->addRow(name, widget);
	};
	const auto button =
		[&](QFormLayout *form, const char *id, const QString &title, const QString &description, std::function<void()> action)
	{
		auto *value = new QPushButton(title);
		value->setObjectName(QString::fromLatin1(id));
		value->setAccessibleName(title);
		value->setAccessibleDescription(description);
		value->setToolTip(description);
		connect(value, &QPushButton::clicked, this,
				[this, action]
				{
					if (!m_working && !m_refreshing)
					{
						action();
					}
				});
		form->addRow(value);
		return value;
	};
	const auto duration = [&](QFormLayout *form, const char *id, const QString &title)
	{
		auto *value = new QDoubleSpinBox;
		value->setDecimals(6);
		value->setRange(.000001, 3600);
		value->setValue(.1);
		value->setSingleStep(.01);
		value->setSuffix(QCoreApplication::translate("VibeStudioModelEditor", " s"));
		field(form, value, id, title,
			  QCoreApplication::translate(
				  "VibeStudioModelEditor",
				  "Hold duration in seconds. Applying changes the selected member and shifts later cumulative end times."));
		return value;
	};
	m_mdlSummary = new QLabel;
	m_mdlSummary->setObjectName(QStringLiteral("meshMdlSummary"));
	m_mdlSummary->setTextFormat(Qt::PlainText);
	m_mdlSummary->setWordWrap(true);
	m_mdlSummary->setMinimumWidth(0);
	m_mdlSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	page->addRow(m_mdlSummary);
	auto *skins = group(QCoreApplication::translate("VibeStudioModelEditor", "Indexed skins"));
	m_mdlSkin = new QComboBox;
	m_mdlMember = new QComboBox;
	field(skins, m_mdlSkin, "meshMdlSkin", QCoreApplication::translate("VibeStudioModelEditor", "Skin slot"),
		  QCoreApplication::translate("VibeStudioModelEditor",
									  "Native skin number used by the game. Removing a slot shifts later skin numbers."));
	field(skins, m_mdlMember, "meshMdlMember", QCoreApplication::translate("VibeStudioModelEditor", "Skin member"),
		  QCoreApplication::translate("VibeStudioModelEditor",
									  "One indexed image inside the selected skin. Preview Member displays it on the model and UV view."));
	m_mdlSkinDuration = duration(skins, "meshMdlSkinDuration", QCoreApplication::translate("VibeStudioModelEditor", "Member duration"));
	const auto import = [&](const char *id, const QString &title, ModelEditKind kind)
	{
		return button(
			skins, id, title,
			QCoreApplication::translate("VibeStudioModelEditor",
										"Import an opaque 256-colour indexed PNG, PCX, LMP, miptexture, WAL or M8. Existing skins require matching dimensions "
										"and palette. Appending to a single image gives both members the chosen duration."),
			[this, kind]
			{
				const auto path =
					QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioModelEditor", "Import Indexed Skin"), {},
												 QCoreApplication::translate("VibeStudioModelEditor", "Indexed skins (*.png *.pcx *.lmp *.mip *.miptex *.wal *.m8)"));
				if (path.isEmpty())
				{
					return;
				}
				QString error;
				if (!importMdlSkin(path, kind, &error))
				{
					m_status->setText(error);
				}
			});
	};
	import("addMeshMdlSkin", QCoreApplication::translate("VibeStudioModelEditor", "Add Skin…"), ModelEditKind::AddMdlSkin);
	m_mdlPackageSkin = button(skins, "importMeshPackageSkin", QCoreApplication::translate("VibeStudioModelEditor", "Import Package Texture…"),
		QCoreApplication::translate("VibeStudioModelEditor", "Copy an indexed texture from the current package, including staged texture edits. Choose an exact entry, then add, replace or append a skin member."),
		[this] { chooseMdlPackageSkin(); });
	m_mdlSkinButtons << import("replaceMeshMdlMember", QCoreApplication::translate("VibeStudioModelEditor", "Replace Member…"),
							   ModelEditKind::ReplaceMdlSkinMember);
	m_mdlSkinButtons << import("appendMeshMdlMember", QCoreApplication::translate("VibeStudioModelEditor", "Append Member…"),
							   ModelEditKind::AppendMdlSkinMember);
	for (const auto &entry :
		 {std::tuple{"removeMeshMdlSkin", QCoreApplication::translate("VibeStudioModelEditor", "Remove Skin"),
					 ModelEditKind::RemoveMdlSkin},
		  std::tuple{"removeMeshMdlMember", QCoreApplication::translate("VibeStudioModelEditor", "Remove Member"),
					 ModelEditKind::RemoveMdlSkinMember},
		  std::tuple{"applyMeshMdlSkinDuration", QCoreApplication::translate("VibeStudioModelEditor", "Apply Member Duration"),
					 ModelEditKind::SetMdlSkinDuration}})
	{
		m_mdlSkinButtons << button(skins, std::get<0>(entry), std::get<1>(entry), std::get<1>(entry),
								   [this, kind = std::get<2>(entry)] { executeMdl(kind); });
	}
	m_mdlSkinButtons << button(
		skins, "previewMeshMdlMember", QCoreApplication::translate("VibeStudioModelEditor", "Preview Member"),
		QCoreApplication::translate(
			"VibeStudioModelEditor",
			"Use this skin member for the model and UV preview until the source or skin selection changes. This does not alter the model."),
		[this] { previewMdlSkin(); });
	m_mdlEnabledButtons << button(
		skins, "loadMeshMdlPalette", QCoreApplication::translate("VibeStudioModelEditor", "Load Palette…"),
		QCoreApplication::translate(
			"VibeStudioModelEditor",
			"Load exactly 768 RGB bytes. Pixel indices remain unchanged; MDL uses the target game's external palette."),
		[this]
		{
			const auto path = QFileDialog::getOpenFileName(
				this, QCoreApplication::translate("VibeStudioModelEditor", "Load MDL Palette"), {},
				QCoreApplication::translate("VibeStudioModelEditor", "RGB palettes (*.lmp *.pal);;All files (*)"));
			if (path.isEmpty())
			{
				return;
			}
			QString error;
			if (!importMdlPalette(path, &error))
			{
				m_status->setText(error);
			}
		});
	auto *frames = group(QCoreApplication::translate("VibeStudioModelEditor", "Native frames"));
	m_mdlGroup = new QComboBox;
	field(frames, m_mdlGroup, "meshMdlGroup", QCoreApplication::translate("VibeStudioModelEditor", "Native frame"),
		  QCoreApplication::translate("VibeStudioModelEditor", "One native frame may contain several timed poses. Changing groups shifts "
															   "native frame numbers used by game code; editor clips remain separate."));
	m_mdlFirst = new QSpinBox;
	m_mdlLast = new QSpinBox;
	field(frames, m_mdlFirst, "meshMdlFirst", QCoreApplication::translate("VibeStudioModelEditor", "First pose"),
		  QCoreApplication::translate("VibeStudioModelEditor", "First pose in the inclusive grouping range."));
	field(frames, m_mdlLast, "meshMdlLast", QCoreApplication::translate("VibeStudioModelEditor", "Last pose"),
		  QCoreApplication::translate("VibeStudioModelEditor", "Last pose in the inclusive grouping range."));
	m_mdlPoseDuration = duration(frames, "meshMdlPoseDuration", QCoreApplication::translate("VibeStudioModelEditor", "Pose duration"));
	m_mdlTiming = new QLabel;
	m_mdlTiming->setObjectName(QStringLiteral("meshMdlTiming"));
	m_mdlTiming->setWordWrap(true);
	m_mdlTiming->setTextFormat(Qt::PlainText);
	m_mdlTiming->setMinimumWidth(0);
	m_mdlTiming->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	frames->addRow(m_mdlTiming);
	for (const auto &entry :
		 {std::tuple{"groupMeshMdlFrames", QCoreApplication::translate("VibeStudioModelEditor", "Group Range"),
					 ModelEditKind::GroupMdlFrames},
		  std::tuple{"ungroupMeshMdlFrames", QCoreApplication::translate("VibeStudioModelEditor", "Ungroup Range"),
					 ModelEditKind::UngroupMdlFrames},
		  std::tuple{"applyMeshMdlPoseDuration", QCoreApplication::translate("VibeStudioModelEditor", "Apply Current Pose Duration"),
					 ModelEditKind::SetMdlFrameDuration}})
	{
		m_mdlEnabledButtons << button(
			frames, std::get<0>(entry), std::get<1>(entry),
			QCoreApplication::translate(
				"VibeStudioModelEditor",
				"Group Range assigns the chosen duration to every pose in the range. Ungroup Range creates separate native frames. Apply "
				"Current Pose Duration changes the pose selected above the viewport."),
			[this, kind = std::get<2>(entry)] { executeMdl(kind); });
	}
	auto *preview = group(QCoreApplication::translate("VibeStudioModelEditor", "Native preview"));
	m_mdlPlaybackTiming = new QComboBox;
	m_mdlPlaybackTiming->setMinimumContentsLength(12);
	m_mdlPlaybackTiming->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_mdlPlaybackTiming->addItems({QCoreApplication::translate("VibeStudioModelEditor", "Stored timing (software Quake)"),
								   QCoreApplication::translate("VibeStudioModelEditor", "Original GLQuake timing")});
	field(preview, m_mdlPlaybackTiming, "meshMdlPlaybackTiming", QCoreApplication::translate("VibeStudioModelEditor", "Timing"),
		  QCoreApplication::translate("VibeStudioModelEditor",
									  "Stored timing uses cumulative group ends. Original GLQuake uses the first pose interval and four "
									  "skin slots at 10 Hz. Native preview always uses exact poses."));
	m_mdlPlaybackTime = new QDoubleSpinBox;
	m_mdlPlaybackTime->setDecimals(6);
	m_mdlPlaybackTime->setRange(0, 86400);
	m_mdlPlaybackTime->setSingleStep(.01);
	field(preview, m_mdlPlaybackTime, "meshMdlPlaybackTime", QCoreApplication::translate("VibeStudioModelEditor", "Seek time (seconds)"),
		  QCoreApplication::translate("VibeStudioModelEditor", "Time from preview start. Seek Time displays an exact pose and skin member "
															   "without starting playback, including with reduced motion enabled."));
	m_mdlSyncPhase = new QDoubleSpinBox;
	m_mdlSyncPhase->setDecimals(6);
	m_mdlSyncPhase->setRange(0, 1);
	m_mdlSyncPhase->setSingleStep(.01);
	field(preview, m_mdlSyncPhase, "meshMdlSyncPhase", QCoreApplication::translate("VibeStudioModelEditor", "Entity phase (seconds)"),
		  QCoreApplication::translate("VibeStudioModelEditor",
									  "Deterministic phase for random-sync models in software Quake. Synchronized models and original "
									  "GLQuake ignore it. This value is not saved in the model."));
	m_mdlSkinButtons << button(
		preview, "previewMeshMdlTiming", QCoreApplication::translate("VibeStudioModelEditor", "Preview Native Timing"),
		QCoreApplication::translate("VibeStudioModelEditor",
									"Prepare the selected skin and native frame, then play from the seek time. Reduced motion keeps the "
									"preview paused. Play / Pause controls this native schedule until a pose or clip is selected."),
		[this] { previewMdlAnimation(true); });
	m_mdlSkinButtons << button(
		preview, "seekMeshMdlTiming", QCoreApplication::translate("VibeStudioModelEditor", "Seek Time"),
		QCoreApplication::translate(
			"VibeStudioModelEditor",
			"Inspect the selected native frame and skin at the chosen time and phase without changing source, history or recovery."),
		[this] { previewMdlAnimation(false); });
	m_mdlEnabledButtons << button(
		preview, "clearMeshMdlTiming", QCoreApplication::translate("VibeStudioModelEditor", "Use Clip Timing"),
		QCoreApplication::translate("VibeStudioModelEditor",
									"Return to the ordinary frame transport, preview FPS and smooth interpolation."),
		[this]
		{
			{
				QScopedValueRollback guard(m_refreshing, true);
				m_preview->clearMdlPlayback();
			}
			refreshAnimationControls();
			refreshMdlPlayback();
			applyMaterialImages();
		});
	m_mdlPlaybackStatus = new QLabel;
	m_mdlPlaybackStatus->setObjectName(QStringLiteral("meshMdlPlaybackStatus"));
	m_mdlPlaybackStatus->setTextFormat(Qt::PlainText);
	m_mdlPlaybackStatus->setWordWrap(true);
	m_mdlPlaybackStatus->setMinimumWidth(0);
	m_mdlPlaybackStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	preview->addRow(m_mdlPlaybackStatus);
	connect(m_preview, &ModelViewport::mdlPlaybackChanged, this,
			[this]
			{
				if (!m_refreshing && !m_working)
				{
					refreshMdlPlayback();
					refreshAnimationControls();
				}
			});
	auto *header = group(QCoreApplication::translate("VibeStudioModelEditor", "Model header"));
	m_mdlFlags = new QLineEdit;
	m_mdlEye = new QLineEdit;
	m_mdlSize = new QLineEdit;
	m_mdlSync = new QComboBox;
	for (auto *value : {m_mdlFlags, m_mdlEye, m_mdlSize})
	{
		value->setLayoutDirection(Qt::LeftToRight);
		value->setMaxLength(128);
	}
	field(header, m_mdlFlags, "meshMdlFlags", QCoreApplication::translate("VibeStudioModelEditor", "Flags"),
		  QCoreApplication::translate(
			  "VibeStudioModelEditor",
			  "Unsigned 32-bit flags, in decimal or 0x hexadecimal. Interpret these using the target game's model effects."));
	field(header, m_mdlEye, "meshMdlEye", QCoreApplication::translate("VibeStudioModelEditor", "Eye position"),
		  QCoreApplication::translate("VibeStudioModelEditor", "Native eye position as x,y,z, using a dot for decimals."));
	field(header, m_mdlSize, "meshMdlSize", QCoreApplication::translate("VibeStudioModelEditor", "Size hint"),
		  QCoreApplication::translate(
			  "VibeStudioModelEditor",
			  "Nonnegative native model size hint, using a dot for decimals. Geometry bounds are calculated during export."));
	m_mdlSync->addItems({QCoreApplication::translate("VibeStudioModelEditor", "Synchronized"),
						 QCoreApplication::translate("VibeStudioModelEditor", "Random phase")});
	field(header, m_mdlSync, "meshMdlSync", QCoreApplication::translate("VibeStudioModelEditor", "Animation sync"),
		  QCoreApplication::translate("VibeStudioModelEditor", "Native sync type, 0 synchronized or 1 random phase."));
	m_mdlEnabledButtons << button(
		header, "applyMeshMdlHeader", QCoreApplication::translate("VibeStudioModelEditor", "Apply Header"),
		QCoreApplication::translate("VibeStudioModelEditor",
									"Save flags, eye position, size hint and synchronization without changing geometry or groups."),
		[this] { executeMdl(ModelEditKind::SetMdlHeader); });
	for (auto *combo : {m_mdlSkin, m_mdlMember, m_mdlGroup, m_mdlPlaybackTiming})
	{
		connect(combo, &QComboBox::currentIndexChanged, this,
				[this]
				{
					if (!m_refreshing && !m_working)
					{
						{
							QScopedValueRollback guard(m_refreshing, true);
							m_preview->clearMdlPlayback();
						}
						refreshMdlControls();
						refreshMdlPlayback();
						refreshAnimationControls();
						applyMaterialImages();
					}
				});
	}
	connect(m_frame, &QComboBox::currentIndexChanged, this,
			[this]
			{
				if (!m_refreshing && !m_working)
				{
					refreshMdlControls();
				}
			});
}

void ModelEditorDialog::refreshMdlControls()
{
	if (!m_mdlSkin || !m_mdlPackageSkin)
	{
		return;
	}
	m_mdlPackageSkin->setEnabled(m_materialSource.archive && m_materialSource.archive->isOpen());
	QScopedValueRollback guard(m_refreshing, true);
	const auto &mesh = m_document.mesh();
	const bool enabled = mesh.mdl.enabled;
	if (m_mdlRevision != m_document.revisionFingerprint())
	{
		m_mdlRevision = m_document.revisionFingerprint();
		m_mdlPreview = {};
		const int slot = m_mdlSkin->currentIndex(), group = m_mdlGroup->currentIndex();
		m_mdlSkin->clear();
		m_mdlGroup->clear();
		for (const auto &skin : mesh.embeddedSkins)
		{
			m_mdlSkin->addItem(QStringLiteral("%1 · %2").arg(skin.index).arg(skin.name));
		}
		for (int i = 0; i < mesh.mdl.frameGroups.size(); ++i)
		{
			const auto &native = mesh.mdl.frameGroups[i];
			m_mdlGroup->addItem(QCoreApplication::translate("VibeStudioModelEditor", "%1 · poses %2–%3")
									.arg(i)
									.arg(native.firstFrame)
									.arg(native.firstFrame + native.frameCount() - 1));
		}
		m_mdlSkin->setCurrentIndex(std::clamp(slot, 0, std::max(0, m_mdlSkin->count() - 1)));
		m_mdlGroup->setCurrentIndex(std::clamp(group, 0, std::max(0, m_mdlGroup->count() - 1)));
		m_mdlFlags->setText(QString::number(mesh.mdl.flags));
		m_mdlEye->setText(QStringLiteral("%1,%2,%3")
							  .arg(mesh.mdl.eyePosition.x, 0, 'g', 9)
							  .arg(mesh.mdl.eyePosition.y, 0, 'g', 9)
							  .arg(mesh.mdl.eyePosition.z, 0, 'g', 9));
		m_mdlSize->setText(QString::number(mesh.mdl.size, 'g', 9));
		m_mdlSync->setCurrentIndex(mesh.mdl.syncType);
		m_displayedMdlSkin = m_displayedMdlMember = m_displayedMdlGroup = m_displayedMdlPose = -2;
	}
	const int slot = m_mdlSkin->currentIndex();
	const bool skinValid = enabled && slot >= 0 && slot < mesh.embeddedSkins.size();
	if (m_displayedMdlSkin != slot)
	{
		const int member = m_mdlMember->currentIndex();
		m_mdlMember->clear();
		m_mdlPreview = {};
		m_displayedMdlMember = -2;
		if (skinValid)
		{
			for (int i = 0; i < mesh.embeddedSkins[slot].indexedFrames.size(); ++i)
			{
				m_mdlMember->addItem(QString::number(i));
			}
		}
		m_mdlMember->setCurrentIndex(std::clamp(member, 0, std::max(0, m_mdlMember->count() - 1)));
		m_displayedMdlSkin = slot;
	}
	const int member = m_mdlMember->currentIndex();
	if (m_displayedMdlMember != member)
	{
		m_mdlPreview = {};
		m_displayedMdlMember = member;
		if (skinValid && member >= 0)
		{
			const auto &times = mesh.embeddedSkins[slot].intervals;
			m_mdlSkinDuration->setValue(times.isEmpty() ? .1 : double(times[member]) - (member ? times[member - 1] : 0));
		}
	}
	for (auto *value : m_mdlEnabledButtons)
	{
		value->setEnabled(enabled);
	}
	for (auto *value : m_mdlSkinButtons)
	{
		value->setEnabled(skinValid);
	}
	findChild<QPushButton *>(QStringLiteral("removeMeshMdlMember"))
		->setEnabled(skinValid && mesh.embeddedSkins[slot].indexedFrames.size() > 1);
	for (auto *value : {m_mdlFirst, m_mdlLast})
	{
		value->setRange(0, std::max(0, int(mesh.frames.size()) - 1));
		value->setEnabled(enabled);
	}
	for (auto *value : {m_mdlFlags, m_mdlEye, m_mdlSize})
	{
		value->setEnabled(enabled);
	}
	m_mdlSync->setEnabled(enabled);
	m_mdlGroup->setEnabled(enabled);
	m_mdlPoseDuration->setEnabled(enabled);
	m_mdlSkin->setEnabled(skinValid);
	m_mdlMember->setEnabled(skinValid);
	m_mdlSkinDuration->setEnabled(skinValid);
	m_mdlPlaybackTiming->setEnabled(skinValid);
	m_mdlPlaybackTime->setEnabled(skinValid);
	m_mdlSyncPhase->setEnabled(skinValid && mesh.mdl.syncType == 1 && m_mdlPlaybackTiming->currentIndex() == 0);
	const int group = m_mdlGroup->currentIndex();
	if (group >= 0 && group < mesh.mdl.frameGroups.size())
	{
		const auto &native = mesh.mdl.frameGroups[group];
		if (m_displayedMdlGroup != group)
		{
			m_mdlFirst->setValue(native.firstFrame);
			m_mdlLast->setValue(native.firstFrame + native.frameCount() - 1);
			m_displayedMdlGroup = group;
		}
		QStringList ends;
		for (float time : native.intervals)
		{
			ends << QString::number(time, 'g', 7);
		}
		m_mdlTiming->setText(
			native.intervals.isEmpty()
				? QCoreApplication::translate("VibeStudioModelEditor", "Single native frame; game code controls its timing.")
				: QCoreApplication::translate(
					  "VibeStudioModelEditor",
					  "Cumulative ends: %1 s. Original GLQuake uses the first interval; software Quake uses the stored times.")
					  .arg(ends.join(QStringLiteral(", "))));
	}
	else
	{
		m_mdlTiming->clear();
	}
	m_mdlTiming->setAccessibleName(m_mdlTiming->text());
	const int pose = m_frame->currentIndex();
	if (pose != m_displayedMdlPose)
	{
		m_displayedMdlPose = pose;
		for (const auto &native : mesh.mdl.frameGroups)
		{
			if (pose < native.firstFrame || pose >= native.firstFrame + native.frameCount())
			{
				continue;
			}
			const int index = pose - native.firstFrame;
			m_mdlPoseDuration->setValue(
				native.intervals.isEmpty() ? .1 : double(native.intervals[index]) - (index ? native.intervals[index - 1] : 0));
			break;
		}
	}
	m_mdlSummary->setText(
		enabled ? QCoreApplication::translate("VibeStudioModelEditor", "%1 × %2 · %3 skins · %4 native frames · %5")
					  .arg(mesh.mdl.skinSize.width())
					  .arg(mesh.mdl.skinSize.height())
					  .arg(mesh.embeddedSkins.size())
					  .arg(mesh.mdl.frameGroups.size())
					  .arg(mesh.mdl.paletteGenerated ? QCoreApplication::translate("VibeStudioModelEditor", "Generated palette")
													 : QCoreApplication::translate("VibeStudioModelEditor", "Imported palette"))
				: QCoreApplication::translate("VibeStudioModelEditor", "MDL settings are created when the first indexed skin is added."));
	m_mdlSummary->setAccessibleName(m_mdlSummary->text());
	refreshMdlPlayback();
}

void ModelEditorDialog::executeMdl(ModelEditKind kind)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.frame = m_frame->currentIndex();
	edit.rangeFirst = m_mdlFirst->value();
	edit.rangeLast = m_mdlLast->value();
	edit.mdlSkinSlot = m_mdlSkin->currentIndex();
	edit.mdlSkinMember = m_mdlMember->currentIndex();
	edit.mdlDuration = kind == ModelEditKind::SetMdlSkinDuration ? m_mdlSkinDuration->value() : m_mdlPoseDuration->value();
	edit.mdlSettings = m_document.mesh().mdl;
	if (kind == ModelEditKind::SetMdlHeader)
	{
		const auto flags = m_mdlFlags->text().trimmed();
		bool valid = false;
		const auto bits = flags.toULongLong(&valid, flags.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10);
		bool parsed = valid && !flags.startsWith('-') && bits <= std::numeric_limits<quint32>::max();
		edit.mdlSettings.flags = quint32(bits);
		edit.mdlSettings.syncType = m_mdlSync->currentIndex();
		edit.mdlSettings.size = m_mdlSize->text().toFloat(&valid);
		parsed &= valid && std::isfinite(edit.mdlSettings.size) && edit.mdlSettings.size >= 0;
		const auto axes = m_mdlEye->text().split(',');
		parsed &= axes.size() == 3;
		if (axes.size() == 3)
		{
			float *target[]{&edit.mdlSettings.eyePosition.x, &edit.mdlSettings.eyePosition.y, &edit.mdlSettings.eyePosition.z};
			for (int i = 0; i < 3; ++i)
			{
				*target[i] = axes[i].toFloat(&valid);
				parsed &= valid && std::isfinite(*target[i]);
			}
		}
		if (!parsed)
		{
			m_status->setText(QCoreApplication::translate(
				"VibeStudioModelEditor", "Use unsigned 32-bit flags, finite x,y,z eye coordinates and a nonnegative size hint."));
			return;
		}
	}
	QString error;
	if (!applyEdit(edit, &error))
	{
		m_status->setText(error);
	}
}

bool ModelEditorDialog::importMdlSkin(const QString &path, ModelEditKind kind, QString *error)
{
	if (kind != ModelEditKind::AddMdlSkin && kind != ModelEditKind::ReplaceMdlSkinMember && kind != ModelEditKind::AppendMdlSkinMember)
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelEditor", "Choose a skin import operation.");
		}
		return false;
	}
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.mdlSkinSlot = m_mdlSkin->currentIndex();
	edit.mdlSkinMember = m_mdlMember->currentIndex();
	edit.mdlDuration = m_mdlSkinDuration->value();
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Import Indexed Skin"),
			[path, edit](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) mutable
			{
				QByteArray bytes;
				return readModelFile(path, &bytes, failure, control) &&
					   decodeModelMdlSkin(path, bytes, modelMdlPreviewPalette(candidate.mesh()), &edit.mdlSkin, failure, control) &&
					   candidate.edit(edit, failure, control);
			},
			error))
	{
		return false;
	}
	refresh();
	if (kind == ModelEditKind::AddMdlSkin)
	{
		m_mdlSkin->setCurrentIndex(m_mdlSkin->count() - 1);
	}
	if (kind == ModelEditKind::AppendMdlSkinMember)
	{
		m_mdlMember->setCurrentIndex(m_mdlMember->count() - 1);
	}
	return true;
}
bool ModelEditorDialog::importMdlPalette(const QString &path, QString *error)
{
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Load MDL Palette"),
			[path](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				ModelEdit edit;
				edit.kind = ModelEditKind::SetMdlPalette;
				edit.selection = candidate.selection();
				return readModelFile(path, &edit.mdlSettings.palette, failure, control) && candidate.edit(edit, failure, control);
			},
			error))
	{
		return false;
	}
	refresh();
	return true;
}
void ModelEditorDialog::previewMdlSkin()
{
	const int slot = m_mdlSkin->currentIndex(), member = m_mdlMember->currentIndex();
	QImage image;
	QString error;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Preview Indexed Skin"),
			[slot, member, &image](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				const auto &mesh = candidate.mesh();
				if (slot < 0 || slot >= mesh.embeddedSkins.size() || member < 0 || member >= mesh.embeddedSkins[slot].indexedFrames.size())
				{
					return false;
				}
				image = modelMdlSkinImage(mesh.embeddedSkins[slot].indexedFrames[member], mesh.mdl.skinSize, mesh.mdl.palette, control);
				return modelWorkCheckpoint(control, ModelWorkPhase::Reading, 1, 1, failure) && !image.isNull();
			},
			&error))
	{
		m_status->setText(error);
		return;
	}
	m_mdlPreview = image;
	{
		QScopedValueRollback guard(m_refreshing, true);
		m_preview->clearMdlPlayback();
	}
	refreshMdlPlayback();
	refreshAnimationControls();
	m_renderMode->setCurrentIndex(3);
	applyMaterialImages();
	m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Previewing skin %1, member %2. The saved source is unchanged.")
						  .arg(slot)
						  .arg(member));
}

void ModelEditorDialog::previewMdlAnimation(bool play)
{
	ModelMdlPlayback request;
	request.nativeFrame = m_mdlGroup->currentIndex();
	request.skin = m_mdlSkin->currentIndex();
	request.timing = m_mdlPlaybackTiming->currentIndex() == 1 ? ModelMdlTiming::GlQuake : ModelMdlTiming::Stored;
	request.seconds = m_mdlPlaybackTime->value();
	request.syncPhase = m_mdlSyncPhase->value();
	QVector<QImage> skins;
	QString error;
	if (m_preview->mdlPlaybackActive() && m_preview->mdlPlayback().skin == request.skin)
	{
		skins = m_preview->mdlPlaybackSkins();
	}
	else if (!performWork(
				 QCoreApplication::translate("VibeStudioModelEditor", "Prepare Native MDL Preview"),
				 [&skins, request](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
				 {
					 ModelMdlPlaybackSample sample;
					 return sampleModelMdl(candidate.mesh(), request, &sample, failure) &&
							prepareModelMdlPlaybackSkins(candidate.mesh(), request.skin, &skins, failure, control);
				 },
				 &error))
	{
		m_status->setText(error);
		return;
	}
	{
		QScopedValueRollback guard(m_refreshing, true);
		if (!m_preview->setMdlPlayback(request, skins, &error))
		{
			m_status->setText(error);
			return;
		}
		m_mdlPreview = {};
		m_renderMode->setCurrentIndex(3);
		m_frame->setCurrentIndex(m_preview->frame());
		if (play)
		{
			m_preview->play();
		}
	}
	if (!m_preview->isPlaying())
	{
		refresh();
	}
	applyMaterialImages();
	refreshMdlPlayback();
	refreshAnimationControls();
	m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Native timing preview. The saved source is unchanged."));
}

void ModelEditorDialog::refreshMdlPlayback()
{
	if (!m_mdlPlaybackStatus)
	{
		return;
	}
	m_mdlPlaybackStatus->setText(m_preview->mdlPlaybackActive()
									 ? m_preview->playbackSummary()
									 : QCoreApplication::translate("VibeStudioModelEditor", "Native timing preview inactive."));
	m_mdlPlaybackStatus->setAccessibleName(m_mdlPlaybackStatus->text());
	if (m_preview->mdlPlaybackActive() && m_renderMode->currentIndex() == 3)
	{
		m_uv->setPlaybackTexture(m_preview->mdlPlaybackSkin());
	}
}
} // namespace vibestudio
