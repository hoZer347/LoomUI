#pragma once

#include "LoomProcess.h"

#include <future>
#include <thread>
#include <vector>


namespace Loom
{
	class Process::ConsoleGuard final
	{
	public:
		explicit ConsoleGuard(void* job, void* desktop = nullptr);
		~ConsoleGuard();

		ConsoleGuard(const ConsoleGuard&) = delete;
		ConsoleGuard& operator=(const ConsoleGuard&) = delete;

	private:
		struct Held
		{
			void* terminal;
			unsigned long long since;
			bool focused;
		};

		void Watch(void* desktop, std::promise<unsigned long> ready);
		void Hide(void* window);
		void Hold(void* window);
		void Expire();
		void Release(void* terminal, bool show);
		void Foreground(void* window);
		bool Holding(void* terminal) const;
		bool InJob(void* window) const;
		bool SpawnedRecently() const;
		bool Handoff(void* window) const;
		bool OwnedOnlyByJob(void* terminal) const;

		static void __stdcall OnEvent(
			void* hook,
			unsigned long event,
			void* window,
			long object,
			long child,
			unsigned long thread,
			unsigned long time);

		void* m_job;
		unsigned long m_thread = 0;
		void* m_foreground = nullptr;
		std::vector<Held> m_held;
		std::thread m_watcher;
	};
};
