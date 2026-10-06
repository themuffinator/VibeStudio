#include "app/level_gestures_dialog.h"
#include "core/level_gestures.h"
#include "core/level_camera_keys.h"
#include "core/studio_settings.h"
#include <QAccessible>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio {
namespace {
// QFormLayout must reserve the full wrapped height inside the scrolling page.
class GestureLabel final : public QLabel {
public:
	GestureLabel(const QString& text, QWidget* parent) : QLabel(text, parent)
	{
		setTextFormat(Qt::PlainText);
		setWordWrap(true);
	}
protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		setMinimumHeight(std::max(0, heightForWidth(width())));
	}
};
}

LevelGesturesDialog::LevelGesturesDialog(StudioSettings& settings, const QString& profile, QWidget* parent)
	: QDialog(parent), m_settings(settings)
{
	editorProfileForId(profile, &m_profile);
	setObjectName(QStringLiteral("levelGesturesDialog"));
	setWindowTitle(tr("Customize %1 Gestures").arg(m_profile.displayName));
	setAccessibleName(windowTitle());
	resize(720, 660);
	auto* layout = new QVBoxLayout(this);
	auto* tabs = new QTabWidget(this);
	tabs->setObjectName(QStringLiteral("gestureTabs"));
	tabs->setAccessibleName(tr("Gesture surface"));
	const auto defaults = levelGestureValues(m_profile.controls);
	auto fields = levelGestureFields();
	const auto rank = [](const LevelGestureField& field) {
		return field.id.startsWith(QLatin1String("camera.flyKeys.")) ? 1
			: field.id.startsWith(QLatin1String("camera.driveKeys.")) ? 2
			: field.kind == LevelGestureFieldKind::ToggleKey || field.kind == LevelGestureFieldKind::HoldKey ? 3 : 0;
	};
	std::stable_sort(fields.begin(), fields.end(), [&](const LevelGestureField& a, const LevelGestureField& b) { return rank(a) < rank(b); });
	for (const auto& section : {QStringLiteral("plan"), QStringLiteral("camera"), QStringLiteral("camera-keys")}) {
		auto* scroll = new QScrollArea(tabs); scroll->setWidgetResizable(true);
		auto* page = new QWidget(scroll);
		auto* form = new QFormLayout(page);
		form->setRowWrapPolicy(QFormLayout::WrapLongRows);
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		for (const auto& field : fields) {
			if (field.section != section) { continue; }
			auto* combo = new QComboBox(page);
			combo->setObjectName(QStringLiteral("gesture.") + field.id);
			combo->setAccessibleName(field.label);
			combo->setAccessibleDescription(tr("An override for this editor profile. Choose Profile default to follow the built-in binding."));
			combo->setToolTip(combo->accessibleDescription());
			if (!levelGestureFieldAvailable(field.id, m_profile.controls)) {
				combo->setEnabled(false);
				combo->setToolTip(tr("Available with a perspective camera profile."));
				combo->setAccessibleDescription(combo->toolTip());
			}
			combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
			combo->setMinimumContentsLength(12);
			combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			QString defaultName;
			for (const auto& option : field.options) { if (option.id == defaults.value(field.id).toString()) { defaultName = option.label; } }
			if (field.kind != LevelGestureFieldKind::Choice && defaults.value(field.id) != QStringLiteral("none")) {
				defaultName = QKeySequence::fromString(defaults.value(field.id).toString(), QKeySequence::PortableText).toString(QKeySequence::NativeText);
			}
			combo->addItem(tr("Profile default (%1)").arg(defaultName), QString());
			for (const auto& option : field.options) { combo->addItem(option.label, option.id); }
			auto* label = new GestureLabel(field.label, page);
			label->setObjectName(QStringLiteral("gestureLabel.") + field.id); label->setBuddy(combo);
			if (field.kind == LevelGestureFieldKind::Choice) { form->addRow(label, combo); }
			else {
				combo->addItem(tr("Custom key"), QStringLiteral("custom"));
				auto* editor = new QKeySequenceEdit(page);
				editor->setObjectName(QStringLiteral("gestureKey.") + field.id);
				editor->setAccessibleName(tr("%1 key").arg(field.label));
				editor->setAccessibleDescription(field.kind == LevelGestureFieldKind::HoldKey
					? tr("Hold one unmodified key for temporary navigation. Releasing it, changing profile or losing focus stops the temporary mode.")
					: field.kind == LevelGestureFieldKind::MotionKey
					? tr("One unmodified direction key. Use None to disable it. Fly keys follow the mouse-look requirement and take priority over drive keys while active.")
					: tr("One mouse-look toggle combination. Escape and Tab remain available for cancellation and focus."));
				editor->setToolTip(editor->accessibleDescription());
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
				editor->setMaximumSequenceLength(1);
#endif
				editor->setEnabled(combo->isEnabled()); editor->hide();
				auto* row = new QVBoxLayout;
				row->setContentsMargins(0, 0, 0, 0); row->addWidget(combo); row->addWidget(editor);
				form->addRow(label, row);
				m_keyEdits.insert(field.id, editor);
				connect(combo, &QComboBox::currentIndexChanged, this, [combo, editor] {
					editor->setVisible(combo->currentData() == QStringLiteral("custom"));
				});
				connect(editor, &QKeySequenceEdit::keySequenceChanged, this, [this] { m_loadError.clear(); validateDraft(); });
			}
			m_fields.insert(field.id, combo);
			connect(combo, &QComboBox::currentIndexChanged, this, [this] { m_loadError.clear(); validateDraft(); });
		}
		scroll->setWidget(page);
		tabs->addTab(scroll, section == QStringLiteral("plan") ? tr("2D Plan") : section == QStringLiteral("camera") ? tr("3D Camera") : tr("Camera Keys"));
	}
	layout->addWidget(tabs, 1);
	m_showOverlaps = new QToolButton(this);
	m_showOverlaps->setObjectName(QStringLiteral("gestureShortcutOverlaps")); m_showOverlaps->setCheckable(true);
	m_showOverlaps->setToolButtonStyle(Qt::ToolButtonTextOnly);
	layout->addWidget(m_showOverlaps, 0, Qt::AlignLeading);
	m_overlaps = new QPlainTextEdit(this); m_overlaps->setObjectName(QStringLiteral("gestureShortcutDetails"));
	m_overlaps->setReadOnly(true); m_overlaps->setAccessibleName(tr("Navigation and command shortcut overlaps"));
	m_overlaps->setMaximumHeight(fontMetrics().height() * 6); m_overlaps->hide();
	layout->addWidget(m_overlaps);
	connect(m_showOverlaps, &QToolButton::toggled, m_overlaps, &QWidget::setVisible);
	m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("gestureStatus")); m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Gesture validation"));
	layout->addWidget(m_status);
	auto* files = new QDialogButtonBox(this);
	auto* import = files->addButton(tr("Import…"), QDialogButtonBox::ActionRole);
	import->setObjectName(QStringLiteral("gestureImport")); import->setAccessibleName(tr("Import gesture preferences"));
	m_export = files->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
	m_export->setObjectName(QStringLiteral("gestureExport")); m_export->setAccessibleName(tr("Export gesture preferences"));
	layout->addWidget(files);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults, this);
	m_apply = buttons->button(QDialogButtonBox::Apply); m_apply->setObjectName(QStringLiteral("gestureApply"));
	m_apply->setAccessibleName(tr("Apply gesture preferences"));
	auto* reset = buttons->button(QDialogButtonBox::RestoreDefaults); reset->setObjectName(QStringLiteral("gestureReset"));
	reset->setAccessibleName(tr("Restore this profile's gesture defaults"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(reset, &QPushButton::clicked, this, [this] { m_loadError.clear(); setDraft({}); });
	connect(m_apply, &QPushButton::clicked, this, [this] {
		QString error;
		if (!m_settings.setEditorGestureOverrides(m_profile.id, draft(), &error)) { setStatus(error); return; }
		setDraft(m_settings.editorGestureOverrides(m_profile.id));
		setStatus(tr("Gesture preferences applied."));
		Q_EMIT preferencesApplied();
	});
	connect(import, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getOpenFileName(this, tr("Import Gestures"), {}, tr("VibeStudio gestures (*.json)"));
		if (path.isEmpty()) { return; }
		QString error; if (!importFile(path, &error)) { setStatus(error); }
	});
	connect(m_export, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, tr("Export Gestures"), m_profile.id + QStringLiteral("-gestures.json"), tr("VibeStudio gestures (*.json)"));
		if (path.isEmpty()) { return; }
		QString error; if (!exportFile(path, true, &error)) { setStatus(error); }
		else { setStatus(tr("Gesture file exported.")); }
	});
	setDraft(settings.editorGestureOverrides(m_profile.id, &m_loadError));
}

QJsonObject LevelGesturesDialog::draft() const
{
	QJsonObject result;
	for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
		auto value = it.value()->currentData().toString();
		if (value == QLatin1String("custom") && m_keyEdits.contains(it.key())) { value = m_keyEdits.value(it.key())->keySequence().toString(QKeySequence::PortableText); result.insert(it.key(), value); }
		else if (!value.isEmpty()) { result.insert(it.key(), value); }
	}
	return result;
}

void LevelGesturesDialog::setDraft(const QJsonObject& overrides)
{
	for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
		const QSignalBlocker blocked(it.value());
		const auto value = overrides.value(it.key()).toString();
		const bool custom = m_keyEdits.contains(it.key()) && !value.isEmpty() && value != QLatin1String("none");
		it.value()->setCurrentIndex(std::max(0, it.value()->findData(custom ? QStringLiteral("custom") : value)));
		if (auto* editor = m_keyEdits.value(it.key())) {
			const QSignalBlocker blockedEditor(editor);
			editor->setKeySequence(custom ? QKeySequence::fromString(value, QKeySequence::PortableText) : QKeySequence());
			editor->setVisible(custom);
		}
	}
	validateDraft();
}

void LevelGesturesDialog::validateDraft()
{
	QString error; QJsonObject normalized; LevelEditorControls effective;
	const bool valid = applyLevelGestureOverrides(m_profile.controls, draft(), &effective, &normalized, &error) && m_loadError.isEmpty();
	m_apply->setEnabled(valid && !m_settings.isReadOnly()); m_export->setEnabled(valid);
	const auto warnings = valid ? levelCameraShortcutWarnings(m_profile, effective, m_settings.userShortcuts()) : QStringList();
	bool inheritedGestureDisabled = false;
	for (const auto& field : {QStringLiteral("camera.materialSampleButton"), QStringLiteral("camera.materialPaintButton"),
		QStringLiteral("camera.surfacePasteFaceButton"), QStringLiteral("camera.surfacePasteBrushButton"), QStringLiteral("camera.surfaceWrapFaceButton"),
		QStringLiteral("camera.surfaceValuesButton"), QStringLiteral("camera.surfaceValuesOnlyButton"), QStringLiteral("camera.surfaceWrapOnlyButton"),
		QStringLiteral("camera.surfaceProjectButton"), QStringLiteral("camera.surfaceProjectOnlyButton")}) {
		inheritedGestureDisabled |= normalized.value(field).toString() == QLatin1String("none") && !draft().contains(field);
	}
	m_showOverlaps->setVisible(!warnings.isEmpty());
	m_showOverlaps->setText(tr("%n possible shortcut overlap(s)", nullptr, static_cast<int>(warnings.size())));
	m_showOverlaps->setAccessibleName(m_showOverlaps->text());
	m_showOverlaps->setAccessibleDescription(tr("Inspect navigation keys that may take priority over command shortcuts in a level view."));
	m_overlaps->setPlainText(warnings.join(QStringLiteral("\n\n")));
	if (warnings.isEmpty()) { m_showOverlaps->setChecked(false); m_overlaps->hide(); }
	if (!m_loadError.isEmpty()) { setStatus(m_loadError); }
	else if (!valid) { setStatus(error); }
	else if (m_settings.isReadOnly()) { setStatus(tr("Preferences are read-only. Valid drafts can still be exported.")); }
	else if (inheritedGestureDisabled) {
		setStatus(tr("Existing preferences disable an inherited surface gesture. Apply saves this choice; surface gestures can be reassigned separately."));
	}
	else { setStatus(tr("%n gesture override(s) ready to apply.", nullptr, static_cast<int>(normalized.size()))); }
}

void LevelGesturesDialog::setStatus(const QString& text)
{
	m_status->setText(text);
	m_status->setAccessibleDescription(text);
	QAccessibleEvent changed(m_status, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&changed);
}

bool LevelGesturesDialog::importFile(const QString& path, QString* error)
{
	QString profile; QJsonObject overrides;
	if (!readLevelGestures(path, &profile, &overrides, error)) { return false; }
	if (profile != m_profile.id) { if (error) { *error = tr("Choose the %1 profile before importing this file.").arg(editorProfileDisplayNameForId(profile)); } return false; }
	m_loadError.clear(); setDraft(overrides); return true;
}

bool LevelGesturesDialog::exportFile(const QString& path, bool overwrite, QString* error) const
{
	return writeLevelGestures(path, m_profile.id, draft(), overwrite, error, m_settings.storageLocation());
}
} // namespace vibestudio
