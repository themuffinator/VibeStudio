#include "app/editor_profile_dialog.h"
#include "core/level_sidebar.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace vibestudio {
namespace {
QString listHtml(const QStringList& values)
{
	QString html = QStringLiteral("<ul>");
	for (const auto& value : values) { html += QStringLiteral("<li>%1</li>").arg(value.toHtmlEscaped()); }
	return html + QStringLiteral("</ul>");
}
}

EditorProfileDialog::EditorProfileDialog(const QString& currentProfileId, QWidget* parent)
	: QDialog(parent), m_profiles(editorProfileDescriptors())
{
	setObjectName(QStringLiteral("editorProfileBrowser"));
	setWindowTitle(tr("Choose Editor Profile"));
	setAccessibleName(windowTitle());
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	resize(940, 680);
	auto* layout = new QVBoxLayout(this);
	auto* filter = new QLineEdit(this);
	filter->setObjectName(QStringLiteral("editorProfileSearch"));
	filter->setPlaceholderText(tr("Search editors, aliases or engine families"));
	filter->setAccessibleName(tr("Search editor profiles"));
	filter->setClearButtonEnabled(true);
	layout->addWidget(filter);
	auto* splitter = new QSplitter(Qt::Horizontal, this);
	splitter->setChildrenCollapsible(false);
	m_list = new QListWidget(splitter);
	m_list->setObjectName(QStringLiteral("editorProfileChoices"));
	m_list->setAccessibleName(tr("Editor profiles"));
	m_list->setAccessibleDescription(tr("Choose a profile to preview its controls and workflow differences."));
	m_list->setMinimumWidth(180);
	m_list->setWordWrap(true);
	m_preview = new QTextBrowser(splitter);
	m_preview->setObjectName(QStringLiteral("editorProfilePreview"));
	m_preview->setAccessibleName(tr("Profile controls and differences"));
	m_preview->setOpenExternalLinks(true);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 2);
	splitter->setSizes({300, 600});
	layout->addWidget(splitter, 1);
	m_count = new QLabel(this);
	m_count->setObjectName(QStringLiteral("editorProfileCount"));
	layout->addWidget(m_count);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	m_apply = buttons->addButton(tr("Use Profile"), QDialogButtonBox::AcceptRole);
	m_apply->setObjectName(QStringLiteral("editorProfileApply"));
	m_apply->setAccessibleDescription(tr("Apply this editor profile through studio settings."));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	for (const auto& profile : m_profiles) {
		auto* item = new QListWidgetItem(profile.displayName, m_list);
		item->setData(Qt::UserRole, profile.id);
		item->setToolTip(profile.description);
		item->setData(Qt::AccessibleDescriptionRole, profile.description);
	}
	connect(m_list, &QListWidget::currentRowChanged, this, [this] { refreshPreview(); });
	connect(filter, &QLineEdit::textChanged, this, [this](const QString& text) {
		const auto needle = text.trimmed();
		int visible = 0;
		QListWidgetItem* first = nullptr;
		for (int row = 0; row < m_profiles.size(); ++row) {
			const auto& profile = m_profiles.at(row);
			const auto haystack = QStringList{profile.id, profile.displayName, profile.shortName, profile.lineage,
				profile.description, profile.aliases.join(QLatin1Char(' ')), profile.supportedEngineFamilies.join(QLatin1Char(' '))}.join(QLatin1Char(' '));
			auto* item = m_list->item(row);
			const bool match = haystack.contains(needle, Qt::CaseInsensitive);
			item->setHidden(!match);
			if (match) { ++visible; if (!first) { first = item; } }
		}
		if (!m_list->currentItem() || m_list->currentItem()->isHidden()) { m_list->setCurrentItem(first); }
		m_count->setText(tr("%1 of %2 profiles").arg(visible).arg(m_profiles.size()));
		refreshPreview();
	});
	int selected = 0;
	for (int row = 0; row < m_profiles.size(); ++row) {
		if (m_profiles.at(row).id == normalizedEditorProfileId(currentProfileId)) { selected = row; break; }
	}
	m_list->setCurrentRow(selected);
	m_count->setText(tr("%1 profiles").arg(m_profiles.size()));
	filter->setFocus(Qt::OtherFocusReason);
}

QString EditorProfileDialog::selectedProfileId() const
{
	const auto* item = m_list->currentItem();
	return item && !item->isHidden() ? item->data(Qt::UserRole).toString() : QString();
}

void EditorProfileDialog::refreshPreview()
{
	const int row = m_list->currentRow();
	const bool valid = row >= 0 && row < m_profiles.size() && !m_list->item(row)->isHidden();
	m_apply->setEnabled(valid);
	if (!valid) { m_preview->setPlainText(tr("No profiles match your search.")); return; }
	const auto& profile = m_profiles.at(row);
	QString html = QStringLiteral("<h2>%1</h2><p>%2</p><p><b>%3</b> %4</p>")
		.arg(profile.displayName.toHtmlEscaped(), profile.description.toHtmlEscaped(), tr("Default layout:").toHtmlEscaped(),
			levelViewLayoutDisplayName(profile.controls.layout).toHtmlEscaped());
	html += QStringLiteral("<p>%1</p>").arg(tr("Preview shows profile defaults. Saved layout preferences and gesture customisations are managed separately in Settings.").toHtmlEscaped());
	// Where the profile keeps the Levels sidebar tabs, and what it calls them.
	const LevelSidebarArrangement sidebars = levelSidebarArrangementForProfile(profile.id);
	const auto tabNames = [&profile](const QStringList& ids) {
		QStringList names;
		for (const QString& id : ids) {
			names << levelSidebarTabTitle(id, profile.id);
		}
		return names.isEmpty() ? tr("None") : names.join(tr(", "));
	};
	html += QStringLiteral("<h3>%1</h3><p><b>%2</b> %3</p><p><b>%4</b> %5</p>")
				.arg(tr("Sidebars").toHtmlEscaped(), tr("Left:").toHtmlEscaped(), tabNames(sidebars.leading).toHtmlEscaped(), tr("Right:").toHtmlEscaped(),
					tabNames(sidebars.trailing).toHtmlEscaped());
	if (!profile.adaptations.isEmpty()) {
		html += QStringLiteral("<h3>%1</h3>").arg(tr("Workflow differences").toHtmlEscaped()) + listHtml(profile.adaptations);
	}
	QString group;
	for (const auto& control : levelEditorControlRows(profile.controls)) {
		if (control.section == QLatin1String("keys")) { continue; }
		if (control.view != group) {
			if (!group.isEmpty()) { html += QStringLiteral("</table>"); }
			group = control.view;
			html += QStringLiteral("<h3>%1</h3><table cellspacing=\"6\" width=\"100%\">").arg(group.toHtmlEscaped());
		}
		html += QStringLiteral("<tr><td width=\"40%\"><b>%1</b></td><td>%2</td></tr>")
			.arg(control.action.toHtmlEscaped(), control.gesture.toHtmlEscaped());
	}
	if (!group.isEmpty()) { html += QStringLiteral("</table>"); }
	QStringList bindings;
	for (const auto& binding : profile.bindings) {
		if (!binding.implemented || binding.shortcut.isEmpty() || binding.clearsKeys) { continue; }
		bindings << tr("%1 — %2").arg(binding.displayName, binding.shortcut);
	}
	if (!bindings.isEmpty()) { html += QStringLiteral("<h3>%1</h3>").arg(tr("Command shortcuts").toHtmlEscaped()) + listHtml(bindings); }
	if (!profile.referenceUrl.isEmpty()) {
		html += QStringLiteral("<p><a href=\"%1\">%2</a></p>").arg(profile.referenceUrl.toHtmlEscaped(), tr("Profile reference").toHtmlEscaped());
	}
	m_preview->setHtml(html);
}

} // namespace vibestudio
