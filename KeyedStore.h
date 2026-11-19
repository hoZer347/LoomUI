#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string>


namespace Loom
{
	/**
	* Loom::KeyedStore
	* - Text by key, kept in a JSON file across runs, one string per key
	* - Saved a moment after the last change, and on Flush or destruction
	*/
	class KeyedStore final
	{
	public:
		// With no file the texts are only kept in memory. A missing or unreadable file loads as none.
		explicit KeyedStore(std::filesystem::path file);
		~KeyedStore();

		KeyedStore(const KeyedStore&) = delete;
		KeyedStore& operator=(const KeyedStore&) = delete;

		// Empty when the key has no text.
		const std::string& Get(const std::string& key) const;

		// Empty text erases the key. Setting what is already held changes nothing.
		void Set(const std::string& key, std::string text);

		// Saves when something changed and has stayed so for the save delay.
		void Update(std::chrono::steady_clock::time_point now);

		// Saves now when something changed.
		void Flush();

	private:
		bool Save(int attempts);

		std::filesystem::path m_file;
		std::map<std::string, std::string> m_texts{ };
		bool m_dirty = false;
		std::chrono::steady_clock::time_point m_changed{ };
	};
};
