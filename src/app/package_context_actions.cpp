#include "app/application_shell.h"
#include "app/level_texture_audit_panel.h"
#include "app/ui_primitives.h"

#include <QDir>
#include <QFileInfo>
#include <QListWidget>

namespace vibestudio {

void ApplicationShell::refreshWorkspacePackageContext()
{
	if (!m_workspaceDrawer) { return; }
	auto sections = m_workspaceDrawer->sections();
	for (auto& section : sections) {
		if (section.id != QStringLiteral("packages")) { continue; }
		QStringList lines;
		if (!m_packageStaging.isLoaded()) {
			section.summary = tr("No package"); section.state = OperationState::Warning;
			lines << tr("No package is mounted yet.") << tr("Open a package file or folder to connect package context to the workspace.");
		} else if (!packageViewArchive().isOpen()) {
			section.summary = tr("Unavailable"); section.state = OperationState::Failed;
			lines << tr("Package view unavailable") << packageViewArchive().errorString();
		} else {
			const auto summary = packageViewArchive().summary();
			section.summary = packageArchiveFormatDisplayName(summary.format);
			section.state = summary.warningCount ? OperationState::Warning : OperationState::Completed;
			lines << tr("Source: %1").arg(summary.sourcePath.isEmpty() ? tr("Untitled Package") : QDir::toNativeSeparators(summary.sourcePath));
			lines << tr("Format: %1").arg(section.summary);
			lines << tr("Entries: %1").arg(summary.entryCount);
			lines << tr("Warnings: %1").arg(summary.warningCount);
			lines << tr("Staged edits: %n", nullptr, m_packageStaging.operations().size());
		}
		section.content = lines.join('\n');
		const auto selected = m_workspaceDrawer->currentSectionId();
		m_workspaceDrawer->setSections(sections); m_workspaceDrawer->showSection(selected);
		return;
	}
}

void ApplicationShell::refreshLevelTextureAudit()
{
	if (!m_levelTextureAudit || !m_levelMapValidation) { return; }
	const QString key = QStringLiteral("%1|%2|%3|%4").arg(packageViewKey()).arg(m_levelMapLoadSerial)
		.arg(m_levelMapDocument.revision).arg(int(m_levelMapDocument.format));
	m_levelTextureAudit->setSource(key, m_levelMapDocument, m_packageStaging.isLoaded()
		? std::make_shared<PackageArchive>(packageViewArchive()) : nullptr, m_settings.accessibilityPreferences().reducedMotion);
	applyLevelTextureAudit();
}

void ApplicationShell::applyLevelTextureAudit()
{
	if (!m_levelTextureAudit || !m_levelMapValidation) { return; }
	constexpr int role = Qt::UserRole + 30;
	const QString tag = QStringLiteral("planned-texture-audit");
	int insertion = m_levelMapValidation->count();
	for (int i = m_levelMapValidation->count() - 1; i >= 0; --i) {
		if (m_levelMapValidation->item(i)->data(role).toString() == tag) {
			insertion = i; delete m_levelMapValidation->takeItem(i);
		}
	}
	const auto add = [&](const QString& text, OperationState state, const QString& tooltip = QString()) {
		auto* item = new QListWidgetItem(text);
		item->setData(role, tag); item->setData(Qt::UserRole + 2, operationStateId(state));
		item->setData(Qt::AccessibleTextRole, text); item->setToolTip(tooltip);
		m_levelMapValidation->insertItem(insertion++, item);
	};
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) { return; }
	const auto& result = m_levelTextureAudit->result();
	if (!result) {
		if (m_levelTextureAudit->state() == OperationState::Failed) {
			add(tr("TEXTURE CHECK [Unavailable]\n%1").arg(m_levelTextureAudit->error()), OperationState::Failed);
		}
		return;
	}
	const auto& audit = *result;
	if (!audit.complete) { add(tr("Texture check incomplete; partial results cannot confirm that every texture resolves."), OperationState::Failed); }
	const QString source = audit.packageSource.isEmpty() ? tr("Untitled Package") : QFileInfo(audit.packageSource).fileName();
	if (audit.missingCount > 0) {
		add(tr("MISSING TEXTURES [%1]\n%n name(s) referenced by this map are not in %2.", nullptr, audit.missingCount)
			.arg(operationStateDisplayName(OperationState::Warning), source), OperationState::Warning);
		int shown = 0;
		for (const auto& reference : audit.references) {
			if (!reference.isMissing()) { continue; }
			if (shown++ >= 40) {
				add(tr("%n further missing texture(s) not listed.", nullptr, audit.missingCount - shown + 1), OperationState::Warning); break;
			}
			add(tr("  %1 — %n use(s)", nullptr, reference.useCount).arg(reference.textureName), OperationState::Warning,
				tr("Searched: %1").arg(reference.candidatePaths.join(QStringLiteral(", "))));
		}
	} else if (audit.complete && audit.uniqueCount > 0) {
		add(tr("TEXTURES [%1]\nAll %n referenced name(s) resolve against %2.", nullptr, audit.uniqueCount)
			.arg(operationStateDisplayName(OperationState::Completed), source), OperationState::Completed);
	}
	for (qsizetype at = 0; at < qMin<qsizetype>(audit.warnings.size(), 10); ++at) { add(audit.warnings.at(at), OperationState::Warning); }
	if (audit.warnings.size() > 10) { add(tr("%n further texture warning(s) not listed.", nullptr, audit.warnings.size() - 10), OperationState::Warning); }
}

} // namespace vibestudio
