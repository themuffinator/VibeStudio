#include "core/process_tree.h"

#include <QProcess>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace vibestudio {

ProcessTree::ProcessTree(QProcess* process)
	: m_process(process)
{
#if defined(Q_OS_WIN)
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (job) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
			m_job = job;
		} else {
			CloseHandle(job);
		}
	}
#elif defined(Q_OS_UNIX)
	if (m_process) {
		// Runs in the child between fork and exec: only async-signal-safe calls belong here.
		m_process->setChildProcessModifier([] { ::setpgid(0, 0); });
	}
#endif
}

ProcessTree::~ProcessTree()
{
#if defined(Q_OS_WIN)
	if (m_job) {
		CloseHandle(static_cast<HANDLE>(m_job));
	}
#endif
}

void ProcessTree::adopt()
{
	if (!m_process || m_process->state() == QProcess::NotRunning) {
		return;
	}
#if defined(Q_OS_WIN)
	if (m_job) {
		HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process->processId()));
		if (process) {
			m_adopted = AssignProcessToJobObject(static_cast<HANDLE>(m_job), process) != 0;
			CloseHandle(process);
		}
	}
#elif defined(Q_OS_UNIX)
	m_adopted = true;
#endif
}

void ProcessTree::kill()
{
	if (!m_process) {
		return;
	}
#if defined(Q_OS_WIN)
	if (m_adopted && m_job) {
		TerminateJobObject(static_cast<HANDLE>(m_job), 1);
		return;
	}
#elif defined(Q_OS_UNIX)
	if (m_adopted && m_process->state() != QProcess::NotRunning) {
		const qint64 group = m_process->processId();
		if (group > 0 && ::kill(-pid_t(group), SIGKILL) == 0) {
			return;
		}
	}
#endif
	m_process->kill();
}

} // namespace vibestudio
