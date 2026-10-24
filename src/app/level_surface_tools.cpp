#include "app/level_surface_tools.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"
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
#include <algorithm>

namespace vibestudio {
QStringList LevelSurfaceTools::commandIds()
{
	return {QStringLiteral("map.surfaceShiftLeft"), QStringLiteral("map.surfaceShiftRight"),
		QStringLiteral("map.surfaceShiftDown"), QStringLiteral("map.surfaceShiftUp"),
		QStringLiteral("map.surfaceRotateLeft"), QStringLiteral("map.surfaceRotateRight"),
		QStringLiteral("map.surfaceShrinkU"), QStringLiteral("map.surfaceGrowU"),
		QStringLiteral("map.surfaceShrinkV"), QStringLiteral("map.surfaceGrowV"),
		QStringLiteral("map.surfaceFit"), QStringLiteral("map.surfaceCenter"),
		QStringLiteral("map.surfaceFlipU"), QStringLiteral("map.surfaceFlipV"),
		QStringLiteral("map.surfaceFitU"), QStringLiteral("map.surfaceFitV"),
		QStringLiteral("map.surfaceJustifyLeft"), QStringLiteral("map.surfaceJustifyRight"),
		QStringLiteral("map.surfaceJustifyTop"), QStringLiteral("map.surfaceJustifyBottom"),
		QStringLiteral("map.surfaceNatural"), QStringLiteral("map.surfaceFaceAligned"),
		QStringLiteral("map.surfaceScaleStepLeft"), QStringLiteral("map.surfaceScaleStepRight"),
		QStringLiteral("map.surfaceScaleStepDown"), QStringLiteral("map.surfaceScaleStepUp")};
}
LevelSurfaceTools::LevelSurfaceTools(QWidget* parent) : QScrollArea(parent)
{
	setObjectName(QStringLiteral("levelSurfaceTools"));
	setAccessibleName(tr("Surface tools"));
	setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget;
	auto* sections = new QVBoxLayout(body);
	sections->setContentsMargins(0, 0, 0, 0); sections->setSpacing(6);
	// Each section's controls, laid out on a body widget of its own.
	const auto sectionLayout = [] {
		auto* inner = new QVBoxLayout(new QWidget);
		inner->setContentsMargins(0, 0, 0, 0); inner->setSpacing(6);
		return inner;
	};
	QVBoxLayout* const target = sectionLayout();
	QVBoxLayout* const adjust = sectionLayout();
	QVBoxLayout* const clipboard = sectionLayout();
	m_target = new QComboBox;
	m_target->setObjectName(QStringLiteral("surfaceQuickTarget"));
	m_target->setAccessibleName(tr("Surface edit target"));
	m_target->addItems({tr("Selection"), tr("Picked Faces")});
	m_target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_target->setMinimumContentsLength(12);
	m_target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_target->setToolTip(tr("Selection includes selected objects in object mode, or picked brush faces in Face mode, plus independent Select Textured faces and whole patches. Vertex/Edge picks alone have no surface targets. Picked Faces uses independent, component or camera face picks, then the current face in the Inspector."));
	m_target->setAccessibleDescription(m_target->toolTip());
	target->addWidget(m_target);
	m_summary = new QLabel; m_summary->setObjectName(QStringLiteral("surfaceQuickSummary"));
	m_summary->setWordWrap(true); m_summary->setTextFormat(Qt::PlainText);
	m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_summary->setAccessibleName(tr("Surfaces affected by quick edits")); target->addWidget(m_summary);
	m_status = new QLabel(tr("Select brushes or patches to adjust their texture mapping."));
	m_status->setObjectName(QStringLiteral("surfaceQuickStatus")); m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Surface edit status")); target->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing surface edits")); target->addWidget(m_progress); m_progress->hide();
	m_cancel = new QToolButton; m_cancel->setObjectName(QStringLiteral("surfaceQuickCancel"));
	m_cancel->setText(tr("Cancel")); m_cancel->setAccessibleName(tr("Cancel pending surface edits"));
	m_cancel->setToolTip(tr("Cancel pending adjustments or paste. Completed batches remain in map undo history."));
	m_cancel->setEnabled(false); target->addWidget(m_cancel);

	m_clipboard = new QLabel(tr("No copied surface"));
	m_clipboard->setObjectName(QStringLiteral("surfaceClipboardSummary"));
	m_clipboard->setAccessibleName(tr("Copied surface")); m_clipboard->setWordWrap(true);
	m_clipboard->setTextFormat(Qt::PlainText); m_clipboard->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	clipboard->addWidget(m_clipboard);
	auto* clipboardRow = new QHBoxLayout; m_rows << clipboardRow;
	for (const auto& id : {QStringLiteral("map.copySurface"), QStringLiteral("map.pasteSurface")}) {
		auto* button = new QToolButton; button->setObjectName(id);
		button->setText(id == QLatin1String("map.copySurface") ? tr("Copy") : tr("Paste"));
		button->setProperty("fullText", button->text());
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_buttons.insert(id, button); clipboardRow->addWidget(button);
	}
	clipboard->addLayout(clipboardRow);
	m_pasteMode = new QComboBox; m_pasteMode->setObjectName(QStringLiteral("surfacePasteMode"));
	m_pasteMode->setAccessibleName(tr("Surface paste mapping"));
	m_pasteMode->addItems({tr("Matching-format parameters"), tr("World projection"), tr("Seamless wrap"), tr("Radiant values"), tr("Radiant projection"), tr("Face attributes")});
	m_pasteMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_pasteMode->setMinimumContentsLength(12);
	m_pasteMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_pasteMode->setToolTip(tr("Face attributes retains contents, rotates each Valve target's existing axes and copies primitive matrices without rescaling. Parameters reuse native values; Radiant values retains each Valve face's axes. Radiant projection copies classic/Valve parameters, projects primitive matrices and patch UVs, and can leave perpendicular faces edge-on. World projection keeps world UVs in any brush format. Seamless wrap turns mapping around the shared edge; a single wrapped face becomes the next source."));
	clipboard->addWidget(m_pasteMode);
	m_mappingOnly = new QCheckBox; m_mappingOnly->setObjectName(QStringLiteral("surfacePasteMappingOnly"));
	m_mappingOnly->setAccessibleName(tr("Keep target materials and flags"));
	m_mappingOnly->setToolTip(tr("Transfer only mapping. Face attributes copies raw primitive matrices; other primitive transfer modes retain texel density using source and target image dimensions. Radiant values keeps Valve axes and leaves patches unchanged. Radiant projection changes patch UVs while retaining materials."));
	auto* mappingLabel = new QLabel(tr("&Keep materials and flags")); mappingLabel->setWordWrap(true); mappingLabel->setBuddy(m_mappingOnly);
	mappingLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	auto* mappingRow = new QHBoxLayout; mappingRow->addWidget(m_mappingOnly); mappingRow->addWidget(mappingLabel, 1); clipboard->addLayout(mappingRow);
	m_allowValve = new QCheckBox; m_allowValve->setObjectName(QStringLiteral("surfacePasteAllowValve"));
	m_allowValve->setAccessibleName(tr("Allow map-wide Valve 220 conversion for surface paste"));
	m_allowValve->setToolTip(tr("If projected or wrapped mapping requires explicit axes, convert all classic faces in this map in the same undo step. Unpasted materials and UVs stay unchanged; locked faces block conversion. Verify support in your game's compiler."));
	m_allowValve->setEnabled(false);
	auto* conversionLabel = new QLabel(tr("&Allow map-wide Valve 220 conversion")); conversionLabel->setWordWrap(true); conversionLabel->setBuddy(m_allowValve);
	conversionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); conversionLabel->setEnabled(false);
	auto* conversionRow = new QHBoxLayout; conversionRow->addWidget(m_allowValve); conversionRow->addWidget(conversionLabel, 1); clipboard->addLayout(conversionRow);
	connect(m_pasteMode, &QComboBox::currentIndexChanged, this, [this, conversionLabel] {
		const bool convertible = m_pasteMode->currentIndex() == 1 || m_pasteMode->currentIndex() == 2;
		m_allowValve->setEnabled(convertible); conversionLabel->setEnabled(convertible);
	});
	const auto step = [&](const QString& title, const QString& id, double value, double maximum, const QString& suffix) {
		auto* label = new QLabel(title); label->setWordWrap(true); adjust->addWidget(label);
		auto* control = new QDoubleSpinBox;
		control->setObjectName(id); control->setAccessibleName(title);
		control->setAccessibleDescription(tr("Amount used by the surface buttons and their keyboard shortcuts."));
		control->setLayoutDirection(Qt::LeftToRight); control->setRange(0.001, maximum);
		control->setDecimals(3); control->setValue(value); control->setSuffix(suffix);
		control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		control->setKeyboardTracking(false); label->setBuddy(control); adjust->addWidget(control); return control;
	};
	const auto pair = [&](int first, const QString& left, const QString& right) {
		auto* row = new QHBoxLayout;
		m_rows << row;
		const auto ids = commandIds();
		for (int i = 0; i < 2; ++i) {
			auto* button = new QToolButton;
			button->setObjectName(ids[first + i]); button->setText(i == 0 ? left : right);
			button->setProperty("fullText", button->text());
			button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			m_buttons.insert(ids[first + i], button); row->addWidget(button);
		}
		adjust->addLayout(row);
	};
	m_shift = step(tr("Shift step (texels)"), QStringLiteral("surfaceQuickShift"), 8, 4096, {});
	pair(0, tr("U −"), tr("U +")); pair(2, tr("V −"), tr("V +"));
	m_rotation = step(tr("Rotation step"), QStringLiteral("surfaceQuickRotation"), 15, 180, QStringLiteral("°"));
	{
		// Where Rotate − and + turn: the middle of each face, or the texture
		// origin as Radiant's Surface Inspector does. Editor profiles choose
		// the default (NetRadiant Custom: 45° about the texture origin).
		auto* label = new QLabel(tr("Rotate about")); label->setWordWrap(true); adjust->addWidget(label);
		m_rotationAnchor = new QComboBox;
		m_rotationAnchor->setObjectName(QStringLiteral("surfaceQuickRotationAnchor"));
		m_rotationAnchor->setAccessibleName(tr("Rotate about"));
		m_rotationAnchor->addItems({tr("Face centre"), tr("Texture origin")});
		m_rotationAnchor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		m_rotationAnchor->setMinimumContentsLength(12);
		m_rotationAnchor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		m_rotationAnchor->setToolTip(tr("Face centre keeps the middle of each face's texture in place. Texture origin keeps the offsets, so the texture turns about the point where the world origin projects onto the face (coordinate 0,0 on a patch), as Radiant's Surface Inspector turns it."));
		m_rotationAnchor->setAccessibleDescription(m_rotationAnchor->toolTip());
		label->setBuddy(m_rotationAnchor); adjust->addWidget(m_rotationAnchor);
	}
	pair(4, tr("Rotate −"), tr("Rotate +"));
	m_scale = step(tr("Size step"), QStringLiteral("surfaceQuickScale"), 10, 1000, QStringLiteral(" %"));
	m_scale->setToolTip(tr("Grow multiplies texture size by 1 + percent / 100; shrink divides by that factor. Both keep the face centre fixed."));
	pair(6, tr("U ÷"), tr("U ×")); pair(8, tr("V ÷"), tr("V ×"));
	m_scaleStep = step(tr("Scale step"), QStringLiteral("surfaceQuickScaleStep"), 0.5, 100, {});
	m_scaleStep->setToolTip(tr("Scale U and V − and + subtract this amount from, or add it to, each face's texture scale value about the texture origin, as Radiant's Surface Inspector does; a scale that would reach zero flips sign instead. Patches have no scale value: + multiplies their texture coordinates by the step and − divides them by it."));
	pair(22, tr("Scale U −"), tr("Scale U +")); pair(24, tr("Scale V −"), tr("Scale V +"));
	pair(10, tr("Fit 1 × 1"), tr("Centre"));
	pair(12, tr("Flip U"), tr("Flip V"));
	pair(14, tr("Fit U"), tr("Fit V"));
	pair(16, tr("Justify Left"), tr("Justify Right"));
	pair(18, tr("Justify Top"), tr("Justify Bottom"));
	pair(20, tr("Natural"), tr("Align to Face"));
	// Bordered like the sidebar's other buttons, so they read as buttons
	// rather than as the flat tool buttons of a tool bar.
	for (QToolButton* button : std::as_const(m_buttons)) { button->setProperty("sidebarButton", true); }
	m_cancel->setProperty("sidebarButton", true);
	const std::pair<QString, QString> titles[] = {{QStringLiteral("surfaces.target"), tr("Target")},
		{QStringLiteral("surfaces.adjust"), tr("Adjust")}, {QStringLiteral("surfaces.clipboard"), tr("Copy and Paste")}};
	const QVBoxLayout* contents[] = {target, adjust, clipboard};
	for (int index = 0; index < 3; ++index) {
		auto* section = new SidebarSection(titles[index].first, titles[index].second, contents[index]->parentWidget());
		section->setStretch(0); m_sections << section; sections->addWidget(section);
	}
	sections->addStretch(); setWidget(body);
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
	// The rows sit in sections, inset by each section's frame and margins.
	const int width = viewport()->width() - 18;
	// What a button needs for its whole label, whatever it shows now.
	const auto chrome = [](const QToolButton* button) { return button->sizeHint().width() - button->fontMetrics().horizontalAdvance(button->text()); };
	const auto whole = [&chrome](const QToolButton* button) {
		return chrome(button) + button->fontMetrics().horizontalAdvance(button->property("fullText").toString());
	};
	for (auto* row : m_rows) {
		auto* first = qobject_cast<QToolButton*>(row->itemAt(0)->widget());
		auto* second = qobject_cast<QToolButton*>(row->itemAt(1)->widget());
		if (!first || !second) { continue; }
		const int pairWidth = whole(first) + whole(second) + row->spacing();
		const auto direction = pairWidth > width ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
		if (row->direction() != direction) { row->setDirection(direction); }
		// A label longer than the panel is wide, at large text or in a long
		// translation, ends in an ellipsis; the tooltip and accessible name
		// keep the command's whole name.
		for (QToolButton* button : {first, second}) {
			const QString full = button->property("fullText").toString();
			const QString shown = button->fontMetrics().elidedText(full, Qt::ElideRight, std::max(1, width - chrome(button)));
			if (button->text() != shown) { button->setText(shown); }
		}
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
void LevelSurfaceTools::setTargetSummary(int faces, LevelSurfaceFace inspected, int patches, bool components)
{
	QString text = components && faces == 0 && (singleFace() || patches == 0) ? tr("No brush-face components selected.")
		: singleFace() ? (faces > 1 ? tr("%n picked face(s)", nullptr, faces)
		: faces == 1 ? tr("Brush %1 · Face %2").arg(inspected.brushId).arg(inspected.faceIndex + 1)
		: tr("Pick faces in the camera, or a face in the Inspector.")) : tr("%n brush face(s) selected", nullptr, faces);
	if (!singleFace() && patches > 0) { text += QLatin1Char('\n') + tr("%n patch(es) selected", nullptr, patches); }
	if (text == m_summary->text()) { return; }
	m_summary->setText(text); m_summary->setAccessibleDescription(text);
	QAccessibleEvent changed(m_summary,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
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
	result.mode = m_pasteMode->currentIndex() == 5 ? LevelSurfacePasteMode::Attributes : m_pasteMode->currentIndex() == 4 ? LevelSurfacePasteMode::RadiantProject : m_pasteMode->currentIndex() == 3 ? LevelSurfacePasteMode::RadiantValues : m_pasteMode->currentIndex() == 2 ? LevelSurfacePasteMode::Seamless
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
		result.operation = rotatesAboutOrigin() ? LevelSurfaceOperation::RadiantRotate : LevelSurfaceOperation::Rotate;
		result.degrees = m_rotation->value() * (index == 4 ? -1 : 1);
	} else if (index < 10) {
		result.operation = LevelSurfaceOperation::Scale;
		const double factor = 1 + m_scale->value() / 100;
		const double change = index % 2 ? factor : 1 / factor;
		if (index < 8) { result.x = change; } else { result.y = change; }
	} else if (index == 11) {
		result.operation = LevelSurfaceOperation::Align;
	} else if (index == 12 || index == 13) {
		// A flip is a scale of -1 about the middle.
		result.operation = LevelSurfaceOperation::Scale;
		result.x = index == 12 ? -1 : 1;
		result.y = index == 13 ? -1 : 1;
	} else if (index == 14 || index == 15) {
		// A repeat count of zero keeps that axis.
		result.x = index == 14 ? 1 : 0;
		result.y = index == 15 ? 1 : 0;
	} else if (index >= 16 && index <= 19) {
		// Texture V runs down the image, so its top is the minimum.
		result.operation = LevelSurfaceOperation::Align;
		result.alignU = index == 16 ? LevelSurfaceAlignment::Minimum : index == 17 ? LevelSurfaceAlignment::Maximum : LevelSurfaceAlignment::Keep;
		result.alignV = index == 18 ? LevelSurfaceAlignment::Minimum : index == 19 ? LevelSurfaceAlignment::Maximum : LevelSurfaceAlignment::Keep;
	} else if (index == 20 || index == 21) {
		result.operation = index == 20 ? LevelSurfaceOperation::Natural : LevelSurfaceOperation::FaceAligned;
		result.x = result.y = m_defaultScale;
	} else if (index >= 22 && index <= 25) {
		// U −, U +, V −, V +: NetRadiant Custom's TexScaleLeft, Right, Down, Up.
		result.operation = LevelSurfaceOperation::RadiantScale;
		const double amount = m_scaleStep->value() * (index % 2 ? 1 : -1);
		result.x = index < 24 ? amount : 0;
		result.y = index < 24 ? 0 : amount;
	}
	return result;
}
bool LevelSurfaceTools::rotatesAboutOrigin() const { return m_rotationAnchor->currentIndex() == 1; }
void LevelSurfaceTools::setProfileSteps(double rotationDegrees, bool aboutOrigin)
{
	m_rotation->setValue(rotationDegrees);
	m_rotationAnchor->setCurrentIndex(aboutOrigin ? 1 : 0);
}
} // namespace vibestudio
