#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

namespace vibestudio {
LevelMapClipOptions ApplicationShell::levelClipOptions() const
{
	return {m_levelClipPreferences.material == LevelClipMaterial::Custom ? m_levelClipPreferences.customTexture
		: m_levelClipPreferences.material == LevelClipMaterial::Current ? levelBrushMaterial() : QString{},
		m_levelClipPreferences.wrapMerge,m_levelClipPreferences.wrapMerge ? levelBrushMaterial() : QString{}};
}
void ApplicationShell::startLevelQuickClip()
{
	if (!levelMapSelectionClippable()) { statusBar()->showMessage(tr("Select brushes, or a brush entity, to clip.")); return; }
	setLevelMapClipMode(true);
	if (!m_levelMapViewport || !m_levelMapViewport->clipMode()) { return; }
	m_levelMapViewport->clearClipLine(); m_levelQuickClip = true;
	statusBar()->showMessage(tr("Quick clip: place the plane and apply. A successful cut exits the tool; Escape cancels."));
}
void ApplicationShell::editLevelClipperPreferences()
{
	// Opening preferences cancels a queued Apply while retaining the plane.
	if (m_levelClipPreferencesChanged) { m_levelClipPreferencesChanged(); }
	QDialog dialog(this); dialog.setObjectName(QStringLiteral("clipperOptionsDialog"));
	dialog.setWindowTitle(tr("Clipper Options")); dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); form->setRowWrapPolicy(QFormLayout::WrapAllRows); layout->addLayout(form);
	auto* material = new QComboBox(&dialog); material->setObjectName(QStringLiteral("clipperMaterial")); material->setAccessibleName(tr("Cut material"));
	material->addItems({tr("Each brush's majority material"),tr("Current material"),tr("Custom / caulk material")}); form->addRow(tr("Cut &material"),material);
	auto* texture = new QLineEdit(&dialog); texture->setObjectName(QStringLiteral("clipperTexture")); texture->setAccessibleName(tr("Custom cut material")); texture->setMaxLength(1024);
	texture->setToolTip(tr("Use a material supplied by this game's assets. For example, common/caulk for a Quake III project that provides it.")); form->addRow(tr("&Custom material"),texture);
	auto* resetPoints = new QCheckBox(tr("Clear &points after a successful cut"),&dialog); resetPoints->setObjectName(QStringLiteral("clipperResetPoints"));
	auto* resetSide = new QCheckBox(tr("Reset to &Back after cutting or entering the tool"),&dialog); resetSide->setObjectName(QStringLiteral("clipperResetSide"));
	auto* twoPoints = new QCheckBox(tr("Use &two points in plan views"),&dialog); twoPoints->setObjectName(QStringLiteral("clipperTwoPoints"));
	twoPoints->setToolTip(tr("After two points, clicking away from a handle starts a new plane. The camera and numeric editor still accept three points."));
	auto* wrap = new QCheckBox(tr("&Wrap/merge when the plane makes no cut"),&dialog); wrap->setObjectName(QStringLiteral("clipperWrapMerge"));
	wrap->setToolTip(tr("One point, a degenerate plane or a cut that changes nothing builds a convex hull around the selected brushes and placed points. This fills gaps. New hull faces use the current material."));
	wrap->setAccessibleDescription(wrap->toolTip());
	for (auto* control : {resetPoints,resetSide,twoPoints,wrap}) { control->setAccessibleName(control->text().remove('&')); form->addRow(control); }
	auto* doubleClick = new QComboBox(&dialog); doubleClick->setObjectName(QStringLiteral("clipperDoubleClick")); doubleClick->setAccessibleName(tr("Double-click action"));
	doubleClick->addItems({tr("No cut (camera matches a face)"),tr("Clip"),tr("Split")});
	doubleClick->setToolTip(tr("Double-click a numbered point to apply. Alt+double-click in the camera always matches a face plane.")); form->addRow(tr("&Double-click"),doubleClick);
	auto* error = new QLabel(&dialog); error->setObjectName(QStringLiteral("clipperOptionsError")); error->setAccessibleName(tr("Clipper settings validation")); error->setWordWrap(true); error->hide(); layout->addWidget(error);
	connect(texture,&QLineEdit::textChanged,error,&QWidget::hide);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,&dialog); layout->addWidget(buttons);
	const auto populate = [&](const LevelClipperPreferences& p) {
		material->setCurrentIndex(int(p.material)); texture->setText(p.customTexture); texture->setEnabled(p.material == LevelClipMaterial::Custom);
		resetPoints->setChecked(p.resetPoints); resetSide->setChecked(p.resetSide); twoPoints->setChecked(p.twoPointsInPlan); doubleClick->setCurrentIndex(int(p.doubleClick));
		wrap->setChecked(p.wrapMerge);
	};
	populate(m_levelClipPreferences);
	connect(material,&QComboBox::currentIndexChanged,&dialog,[&](int i) { texture->setEnabled(i == int(LevelClipMaterial::Custom)); });
	connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&] { populate({}); });
	connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
	connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
		LevelClipperPreferences next; next.material = LevelClipMaterial(material->currentIndex()); next.customTexture = texture->text();
		next.resetPoints = resetPoints->isChecked(); next.resetSide = resetSide->isChecked(); next.twoPointsInPlan = twoPoints->isChecked(); next.doubleClick = LevelClipDoubleClick(doubleClick->currentIndex());
		next.wrapMerge = wrap->isChecked();
		QString message; auto check = next;
		if (!updateLevelClipperPreferences(levelClipperPreferencesJson(next),&check,&message)) { error->setText(message); error->show(); texture->setFocus(); return; }
		if (!m_settings.setLevelClipperPreferences(next)) { error->setText(tr("Unable to save clipper preferences.")); error->show(); return; }
		m_levelClipPreferences = next; if (m_levelClipPreferencesChanged) { m_levelClipPreferencesChanged(); } dialog.accept();
	});
	dialog.exec();
}
} // namespace vibestudio
