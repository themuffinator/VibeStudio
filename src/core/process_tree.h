#pragma once

// Ends a started process together with the processes it started. QProcess::kill() ends only the
// process itself, so a launcher (node running an npm package's script, a shell, a wrapper
// script) leaves its children running, still working and still holding their folders. On
// Windows the process joins a job object that ends everything in it when closed; on Unix it
// leads a process group of its own, and the group is signalled. Elsewhere, or when the
// operating system refuses, it falls back to QProcess::kill().

class QProcess;

namespace vibestudio {

class ProcessTree {
public:
	// Call before process->start(): on Unix the process then starts in its own group.
	explicit ProcessTree(QProcess* process);
	// Ends whatever is left of the tree on Windows (the job is closed).
	~ProcessTree();
	ProcessTree(const ProcessTree&) = delete;
	ProcessTree& operator=(const ProcessTree&) = delete;

	// Call straight after process->start(): on Windows the process joins the job before it has
	// had time to start children.
	void adopt();
	// Ends the process and its descendants.
	void kill();

private:
	QProcess* m_process = nullptr;
	void* m_job = nullptr;
	bool m_adopted = false;
};

} // namespace vibestudio
