// The Materials workbench inside the shell: it reads the package view (staged
// edits included) and saves through the package's staging, so material
// edits join the same undo, review and save as every other package change.

#include "app/application_shell.h"

#include "app/material_workbench.h"
#include "core/package_staging.h"

#include <QDir>
#include <QFileInfo>
#include <QStatusBar>
#include <QTabWidget>

#include <algorithm>

namespace vibestudio {

QWidget* ApplicationShell::buildMaterialWorkbench()
{
	m_materialWorkbench = new MaterialWorkbench;
	const auto packageBusy = [this](QString* error) {
		if (m_packageSaveRunning || m_packageReadRunning || m_packageExtractionRunning) {
			if (error) {
				*error = tr("The package is busy; save the material again when it is idle.");
			}
			return true;
		}
		if (!m_packageStaging.isLoaded()) {
			if (error) {
				*error = tr("Open the package the material belongs to before saving into it.");
			}
			return true;
		}
		return false;
	};
	MaterialWorkbenchHost host;
	host.stageFile = [this, packageBusy](const QByteArray& bytes, const QString& virtualPath, QString* error) {
		if (packageBusy(error)) {
			return false;
		}
		if (m_packageStaging.sourceFormat() == PackageArchiveFormat::Wad) {
			if (error) {
				*error = tr("A WAD holds lumps, not files: %1 cannot be staged into it.").arg(virtualPath);
			}
			return false;
		}
		if (!m_packageStaging.addBytes(bytes, virtualPath, error, PackageStageConflictResolution::ReplaceExisting)) {
			return false;
		}
		refreshPackageStagingSummary();
		recordActivity(tr("Material staged"), virtualPath, QStringLiteral("shader"), OperationState::Warning, tr("Package save pending"));
		syncMaterialWorkbench();
		return true;
	};
	host.stageLump = [this, packageBusy](const QByteArray& bytes, const QString& name, QString* error) {
		if (packageBusy(error)) {
			return false;
		}
		if (m_packageStaging.sourceFormat() != PackageArchiveFormat::Wad) {
			if (error) {
				*error = tr("Only a WAD takes the %1 lump.").arg(name);
			}
			return false;
		}
		if (!m_packageStaging.addWadBytes(bytes, name, QString(), 0, true, error)) {
			return false;
		}
		refreshPackageStagingSummary();
		recordActivity(tr("Lump staged"), name, QStringLiteral("shader"), OperationState::Warning, tr("Package save pending"));
		syncMaterialWorkbench();
		return true;
	};
	host.showTexture = [this](const QString& reference) { showTextureReference(reference); };
	host.openInCode = [this](const QString& path, int line) { openCodeFileAt(path, std::max(1, line)); };
	m_materialWorkbench->setHost(host);
	connect(m_materialWorkbench, &MaterialWorkbench::statusMessage, this, [this](const QString& message) { statusBar()->showMessage(message); });
	connect(m_materialWorkbench, &MaterialWorkbench::contentChanged, this, [this]() { refreshSurfaceStates(); });
	syncMaterialWorkbench();
	return m_materialWorkbench;
}

void ApplicationShell::syncMaterialWorkbench()
{
	if (!m_materialWorkbench) {
		return;
	}
	m_materialWorkbench->setReducedMotion(m_settings.accessibilityPreferences().reducedMotion);
	if (!m_packageArchive.isOpen()) {
		m_materialWorkbench->clearPackage();
		return;
	}
	// The view only changes with the staging revision; copy it only then.
	const QString key = packageViewKey();
	if (key == m_materialWorkbench->packageRevision()) {
		return;
	}
	m_materialWorkbench->setPackage(std::make_shared<PackageArchive>(packageViewArchive()), packageOpenPath(), key);
}

void ApplicationShell::showMaterialsForImage(const QString& virtualPath)
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	setMode(StudioMode::Shaders);
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
	m_materialWorkbench->showMaterialsUsingImage(virtualPath);
}

void ApplicationShell::showMaterialNamed(const QString& name)
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	// Before the page shows (which reads the package), so the material is
	// picked when the library is ready.
	m_materialWorkbench->revealMaterial(name);
	setMode(StudioMode::Shaders);
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
}

void ApplicationShell::openMaterialScript(const QString& path)
{
	if (!m_materialWorkbench) {
		return;
	}
	syncMaterialWorkbench();
	QString error;
	if (!m_materialWorkbench->openScript(path, &error)) {
		statusBar()->showMessage(error);
		return;
	}
	if (m_shaderPageTabs) {
		m_shaderPageTabs->setCurrentIndex(0);
	}
	recordActivity(tr("Material script opened"), QDir::toNativeSeparators(path), QStringLiteral("shader"), OperationState::Completed,
		QFileInfo(path).fileName());
}

} // namespace vibestudio
