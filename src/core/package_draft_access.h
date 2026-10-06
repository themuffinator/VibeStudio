#pragma once

#include <QString>
#include <memory>

namespace vibestudio {

// A directory handle coordinates readers/savers with destructive maintenance.
// It creates no lock file, survives manifest replacement, and is shared by every
// object identity in a draft snapshot. There is one handle per snapshot, not one
// per payload. The OS releases it on process termination.
class PackageDraftAccess final {
public:
	enum class Mode { Read, Maintain };
	static std::shared_ptr<const PackageDraftAccess> acquire(const QString& directory, Mode mode, QString* error = nullptr);
	~PackageDraftAccess();
	[[nodiscard]] bool protectsReaders() const;
	[[nodiscard]] bool matchesDirectory() const;
private:
	struct State;
	explicit PackageDraftAccess(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
};

} // namespace vibestudio
