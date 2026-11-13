#pragma once

#include "Images.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>


struct ImTextureData;

namespace Loom
{
	/**
	* Loom::ImageCache
	* - The images a tab shows. Sizes are read once for laying out, on a thread,
	*   so a long transcript decodes nothing until it scrolls into view
	* - ImGui's backend does the upload, so this works on either renderer.
	*   Textures not drawn for a while go, and come back when next drawn
	* - A key is a file path unless the loader supplies the bytes for it, which it does by returning true
	*   and setting them, empty bytes failing as not an image
	* - A job starts as soon as it is asked for, and finished is called from its thread
	*   when it ends, so the owner can wake a waiting frame loop to take the result
	*/
	struct ImageCache final
	{
		enum class State : uint8_t
		{
			Waiting,
			Ready,
			Failed,
		};

		struct Entry
		{
			State state = State::Waiting;
			int width = 0;
			int height = 0;
			std::string error;
			ImTextureData* texture = nullptr;
			bool decoding = false;
			std::chrono::steady_clock::time_point used{ };
		};

		using Loader = std::function<bool(const std::string& key, std::string& bytes)>;

		explicit ImageCache(std::function<void()> finished = { }, Loader loader = { });
		~ImageCache();

		ImageCache(const ImageCache&) = delete;
		ImageCache& operator=(const ImageCache&) = delete;

		void Update();

		const Entry& Find(const std::string& path);

		const Entry& Use(const std::string& path);

		uint32_t Changes() const { return m_changes; };

	private:
		struct Job
		{
			std::string path;
			bool decode = false;
		};

		struct Finished
		{
			std::string path;
			bool decode = false;
			bool loaded = false;
			int width = 0;
			int height = 0;
			std::string error;
			Images::Pixels pixels;
		};

		static Finished Run(Job job, const Loader& loader);

		void Start();
		void Take(Finished finished);
		void Release(Entry& entry);
		void Bury();

		std::unordered_map<std::string, Entry> m_entries{ };
		std::deque<Job> m_jobs{ };
		std::future<void> m_running{ };
		std::future<Finished> m_result{ };
		std::vector<ImTextureData*> m_dying{ };
		uint32_t m_changes = 0;
		const std::function<void()> m_finished;
		const Loader m_loader;
	};
};
