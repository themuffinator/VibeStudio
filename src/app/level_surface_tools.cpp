#include "app/level_surface_tools.h"
#include "app/studio_actions.h"
#include <QComboBox>
#include <QCheckBox>
#include <QAccessible>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio {
QStringList LevelSurfaceTools::commandIds()
{
	return {QStringLiteral("map.surfaceShiftLeft"), QStringLiteral("map.surfaceShiftRight"),
		QStringLiteral("map.surfaceShiftDown"), QStringLiteral("map.surfaceShiftUp"),
		QStringLiteral("map.surfaceRotateLeft"), QStringLiteral("map.surfaceRotateRight"),
		QStringLiteral("map.surfaceShrinkU"), QStringLiteral("map.surfaceGrowU"),
		QStringLiteral("map.surfaceShrinkV"), QStringLiteral("map.surfaceGrowV"),
		QStringLiteral("map.surfaceFit"), QStringLiteral("map.surfaceCenter")};
}
LevelSurfaceTools::LevelSurfaceTools(QWidget* parent) : QScrollArea(parent)
{
	setObjectName(QStringLiteral("levelSurfaceTools"));
	setAccessibleName(tr("Surface tools"));
	setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setContentsMargins(6, 6, 6, 6);
	m_target = new QComboBox;
	m_target->setObjectName(QStringLiteral("surfaceQuickTarget"));
	m_target->setAccessibleName(tr("Surface edit target"));
	m_target->addItems({tr("Selection"), tr("Face")});
	m_target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_target->setMinimumContentsLength(12);
	m_target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_target->setToolTip(tr("Selection includes faces and patches in selected brushes, patches and entities. Quick adjustments affect brush faces; parameter paste changes patch materials while preserving their UVs. Face uses the current face in the Inspector."));
	layout->addWidget(m_target);
	m_clipboard = new QLabel(tr("No copied surface"));
	m_clipboard->setObjectName(QStringLiteral("surfaceClipboardSummary"));
	m_clipboard->setAccessibleName(tr("Copied surface")); m_clipboard->setWordWrap(true);
	m_clipboard->setTextFormat(Qt::PlainText); m_clipboard->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(m_clipboard);
	auto* clipboardRow = new QHBoxLayout; m_rows << clipboardRow;
	for (const auto& id : {QStringLiteral("map.copySurface"), QStringLiteral("map.pasteSurface")}) {
		auto* button = new QToolButton; button->setObjectName(id);
		button->setText(id == QLatin1String("map.copySurface") ? tr("Copy") : tr("Paste"));
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_buttons.insert(id, button); clipboardRow->addWidget(button);
	}
	layout->addLayout(clipboardRow);
	m_pasteMode = new QComboBox; m_pasteMode->setObjectName(QStringLiteral("surfacePasteMode"));
	m_pasteMode->setAccessibleName(tr("Surface paste mapping"));
	m_pasteMode->addItems({tr("Matching-format parameters"), tr("World projection"), tr("Seamless wrap"), tr("Radiant values"), tr("Radiant projection")});
	m_pasteMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_pasteMode->setMinimumContentsLength(12);
	m_pasteMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_pasteMode->setToolTip(tr("Parameters reuse native values; Radiant values retains each Valve face's axes. Radiant projection copies classic/Valve parameters, projects primitive matrices and patch UVs, and can leave perpendicular faces edge-on. World projection keeps world UVs in any brush format. Seamless wrap turns mapping around the shared edge; a single wrapped face becomes the next source."));
	layout->addWidget(m_pasteMode);
	m_mappingOnly = new QCheckBox; m_mappingOnly->setObjectName(QStringLiteral("surfacePasteMappingOnly"));
	m_mappingOnly->setAccessibleName(tr("Keep target materials and flags"));
	m_mappingOnly->setToolTip(tr("Transfer only mapping. Primitive mappings retain texel density using the source and target image dimensions. Radiant values keeps Valve axes and leaves patches unchanged. Radiant projection changes patch UVs while retaining materials."));
	auto* mappingLabel = new QLabel(tr("&Keep materials and flags")); mappingLabel->setWordWrap(true); mappingLabel->setBuddy(m_mappingOnly);
	mappingLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	auto* mappingRow = new QHBoxLayout; mappingRow->addWidget(m_mappingOnly); mappingRow->addWidget(mappingLabel, 1); layout->addLayout(mappingRow);
	m_allowValve = new QCheckBox; m_allowValve->setObjectName(QStringLiteral("surfacePasteAllowValve"));
	m_allowValve->setAccessibleName(tr("Allow map-wide Valve 220 conversion for surface paste"));
	m_allowValve->setToolTip(tr("If projected or wrapped mapping requires explicit axes, convert all classic faces in this map in the same undo step. Unpasted materials and UVs stay unchanged; locked faces block conversion. Verify support in your game's compiler."));
	m_allowValve->setEnabled(false);
	auto* conversionLabel = new QLabel(tr("&Allow map-wide Valve 220 conversion")); conversionLabel->setWordWrap(true); conversionLabel->setBuddy(m_allowValve);
	conversionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); conversionLabel->setEnabled(false);
	auto* conversionRow = new QHBoxLayout; conversionRow->addWidget(m_allowValve); conversionRow->addWidget(conversionLabel, 1); layout->addLayout(conversionRow);
	connect(m_pasteMode, &QComboBox::currentIndexChanged, this, [this, conversionLabel] {
		const bool convertible = m_pasteMode->currentIndex() == 1 || m_pasteMode->currentIndex() == 2;
		m_allowValve->setEnabled(convertible); conversionLabel->setEnabled(convertible);
	});
	m_summary = new QLabel; m_summary->setObjectName(QStringLiteral("surfaceQuickSummary"));
	m_summary->setWordWrap(true); m_summary->setTextFormat(Qt::PlainText);
	m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_summary->setAccessibleName(tr("Surfaces affected by quick edits")); layout->addWidget(m_summary);
	const auto step = [&](const QString& title, const QString& id, double value, double maximum, const QString& suffix) {
		auto* label = new QLabel(title); label->setWordWrap(true); layout->addWidget(label);
		auto* control = new QDoubleSpinBox;
		control->setObjectName(id); control->setAccessibleName(title);
		control->setAccessibleDescription(tr("Amount used by the surface buttons and their keyboard shortcuts."));
		control->setLayoutDirection(Qt::LeftToRight); control->setRange(0.001, maximum);
		control->setDecimals(3); control->setValue(value); control->setSuffix(suffix);
		control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		control->setKeyboardTracking(false); label->setBuddy(control); layout->addWidget(control); return control;
	};
	const auto pair = [&](int first, const QString& left, const QString& right) {
		auto* row = new QHBoxLayout;
		m_rows << row;
		const auto ids = commandIds();
		for (int i = 0; i < 2; ++i) {
			auto* button = new QToolButton;
			button->setObjectName(ids[first + i]); button->setText(i == 0 ? left : right);
			button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			m_buttons.insert(ids[first + i], button); row->addWidget(button);
		}
		layout->addLayout(row);
	};
	m_shift = step(tr("Shift step (texels)"), QStringLiteral("surfaceQuickShift"), 8, 4096, {});
	pair(0, tr("U −"), tr("U +")); pair(2, tr("V −"), tr("V +"));
	m_rotation = step(tr("Rotation step"), QStringLiteral("surfaceQuickRotation"), 15, 180, QStringLiteral("°"));
	pair(4, tr("Rotate −"), tr("Rotate +"));
	m_scale = step(tr("Size step"), QStringLiteral("surfaceQuickScale"), 10, 1000, QStringLiteral(" %"));
	m_scale->setToolTip(tr("Grow multiplies texture size by 1 + percent / 100; shrink divides by that factor. Rotation and scaling keep the face centre fixed."));
	pair(6, tr("U ÷"), tr("U ×")); pair(8, tr("V ÷"), tr("V ×"));
	pair(10, tr("Fit 1 × 1"), tr("Centre"));
	m_status = new QLabel(tr("Select brush surfaces to adjust their texture mapping."));
	m_status->setObjectName(QStringLiteral("surfaceQuickStatus")); m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Surface edit status")); layout->insertWidget(1, m_status);
	m_progress = new QProgressBar; m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing surface edits")); layout->insertWidget(2, m_progress); m_progress->hide();
	m_cancel = new QToolButton; m_cancel->setObjectName(QStringLiteral("surfaceQuickCancel"));
	m_cancel->setText(tr("Cancel")); m_cancel->setAccessibleName(tr("Cancel pending surface edits"));
	m_cancel->setToolTip(tr("Cancel pending adjustments or paste. Completed batches remain in map undo history."));
	m_cancel->setEnabled(false); layout->insertWidget(3, m_cancel);
	layout->addStretch(); setWidget(body);
	viewport()->installEventFilter(this); body->installEventFilter(this);
	connect(m_target, &QComboBox::currentIndexChanged, this, [this] { if (targetChanged) { targetChanged(); } });
	connect(m_cancel, &QToolButton::clicked, this, [this] { if (cancelRequested) { cancelRequested(); } });
}
bool LevelSurfaceTools::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest || event->type() == QEvent::FontChange) { updateRows(); }
	return QScrollArea::eventFilter(watched, event);
}
void LevelSurfaceTools::updateRows()
{
	const int width = viewport()->width() - 12;
	for (auto* row : m_rows) {
		const int pairWidth = row->itemAt(0)->widget()->sizeHint().width() + row->itemAt(1)->widget()->sizeHint().width() + row->spacing();
		const auto direction = pairWidth > width ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
		if (row->direction() != direction) { row->setDirection(direction); }
	}
}
void LevelSurfaceTools::bindCommands(StudioCommandRegistry& commands)
{
	for (auto it = m_buttons.cbegin(); it != m_buttons.cend(); ++it) {
		auto* action = commands.action(it.key());
		bindCommandButton(it.value(), action);
		if (action) { it.value()->setAccessibleName(commandLabelWithoutMnemonic(action->text())); }
	}
}
bool LevelSurfaceTools::singleFace() const { return m_target->currentIndex() == 1; }
void LevelSurfaceTools::setTargetSummary(int faces, LevelSurfaceFace inspected, int patches)
{
	m_summary->setText(singleFace() ? (faces == 1 ? tr("Brush %1 · Face %2").arg(inspected.brushId).arg(inspected.faceIndex + 1)
		: tr("Select a face in the Inspector.")) : tr("%n brush face(s) selected", nullptr, faces));
	if (!singleFace() && patches > 0) { m_summary->setText(m_summary->text() + QLatin1Char('\n') + tr("%n patch(es) selected", nullptr, patches)); }
}
void LevelSurfaceTools::setStatus(const QString& text, bool busy)
{
	m_status->setText(text); m_progress->setVisible(busy); m_cancel->setEnabled(busy);
	m_status->setAccessibleDescription(text);
	QAccessibleEvent changed(m_status, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
void LevelSurfaceTools::setClipboardSummary(const LevelSurfaceClipboard& clipboard)
{
	const auto format = clipboard.mappingKind() == QLatin1String("matrix") ? tr("Brush matrix")
		: clipboard.mappingKind() == QLatin1String("valve220") ? tr("Valve 220") : tr("Classic");
	const auto text = clipboard.ready() ? tr("%1 · %2 · material, mapping and flags").arg(clipboard.material(), format) : tr("No copied surface");
	if (text == m_clipboard->text()) { return; }
	m_clipboard->setText(text); m_clipboard->setAccessibleDescription(text);
	QAccessibleEvent changed(m_clipboard, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
LevelSurfacePasteOptions LevelSurfaceTools::pasteOptions() const
{
	LevelSurfacePasteOptions result;
	result.mode = m_pasteMode->currentIndex() == 4 ? LevelSurfacePasteMode::RadiantProject : m_pasteMode->currentIndex() == 3 ? LevelSurfacePasteMode::RadiantValues : m_pasteMode->currentIndex() == 2 ? LevelSurfacePasteMode::Seamless
		: m_pasteMode->currentIndex() == 1 ? LevelSurfacePasteMode::Project : LevelSurfacePasteMode::Parameters;
	result.mappingOnly = m_mappingOnly->isChecked();
	result.allowValve220 = (result.mode == LevelSurfacePasteMode::Project || result.mode == LevelSurfacePasteMode::Seamless) && m_allowValve->isChecked(); return result;
}
LevelSurfaceRequest LevelSurfaceTools::request(const QString& command) const
{
	const int index = commandIds().indexOf(command);
	LevelSurfaceRequest result;
	if (index < 4) {
		result.operation = LevelSurfaceOperation::Shift;
		result.x = index == 0 ? -m_shift->value() : index == 1 ? m_shift->value() : 0;
		result.y = index == 2 ? -m_shift->value() : index == 3 ? m_shift->value() : 0;
	} else if (index < 6) {
		result.operation = LevelSurfaceOperation::Rotate;
		result.degrees = m_rotation->value() * (index == 4 ? -1 : 1);
	} else if (index < 10) {
		result.operation = LevelSurfaceOperation::Scale;
		const double factor = 1 + m_scale->value() / 100;
		const double change = index % 2 ? factor : 1 / factor;
		if (index < 8) { result.x = change; } else { result.y = change; }
	} else if (index == 11) { result.operation = LevelSurfaceOperation::Align; }
	return result;
}
} // namespace vibestudio
