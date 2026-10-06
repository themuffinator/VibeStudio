#pragma once

#include <QString>
#include <functional>

namespace vibestudio
{
enum class ModelWorkPhase
{
	Reading,
	Validating,
	Editing,
	Serializing,
	Writing,
	Committing
};

// Callbacks execute on the caller's thread. UI clients publish progress through
// their worker's value state, never by touching widgets from these callbacks.
struct ModelWorkControl
{
	std::function<bool()> cancelled;
	std::function<void(ModelWorkPhase phase, qint64 completed, qint64 total)> progress;
};
bool modelWorkCheckpoint(const ModelWorkControl &control, ModelWorkPhase phase, qint64 completed = 0, qint64 total = 0,
						 QString *error = nullptr);

// Poll long geometry loops in batches, without publishing one progress event
// for every vertex. Call check() before work and before publishing its result.
class ModelWorkProgress
{
  public:
	ModelWorkProgress(const ModelWorkControl &control, ModelWorkPhase phase, QString *error = nullptr)
		: m_control(control), m_phase(phase), m_error(error)
	{
	}
	bool step()
	{
		++m_completed;
		return (m_completed & 255) != 0 || check();
	}
	bool check() const { return modelWorkCheckpoint(m_control, m_phase, m_completed, 0, m_error); }

  private:
	const ModelWorkControl &m_control;
	ModelWorkPhase m_phase;
	QString *m_error;
	qint64 m_completed = 0;
};
} // namespace vibestudio
