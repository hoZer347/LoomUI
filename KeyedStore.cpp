#include "KeyedStore.h"

#include "Files.h"

#include <nlohmann/json.hpp>

#include <utility>


namespace Loom
{
	using nlohmann::json;

	namespace
	{
		constexpr std::chrono::milliseconds save_delay{ 1000 };
		constexpr int flush_attempts = 50;
		constexpr int update_attempts = 1;
		constexpr int json_indent = 1;

		const std::string no_text{ };
	};

	KeyedStore::KeyedStore(std::filesystem::path file) :
		m_file(std::move(file))
	{
		if (m_file.empty())
			return;

		const json parsed = json::parse(Files::ReadWhole(m_file), nullptr, false);

		if (!parsed.is_object())
			return;

		for (const auto& [key, value] : parsed.items())
			if (value.is_string() && !value.get_ref<const std::string&>().empty())
				m_texts[key] = value.get<std::string>();
	};

	KeyedStore::~KeyedStore()
	{
		Flush();
	};

	const std::string& KeyedStore::Get(const std::string& key) const
	{
		const auto found = m_texts.find(key);

		return found == m_texts.end() ? no_text : found->second;
	};

	void KeyedStore::Set(
		const std::string& key,
		std::string text)
	{
		if (Get(key) == text)
			return;

		if (text.empty())
			m_texts.erase(key);
		else m_texts[key] = std::move(text);

		m_dirty = true;
		m_changed = std::chrono::steady_clock::now();
	};

	void KeyedStore::Update(std::chrono::steady_clock::time_point now)
	{
		if (!m_dirty || now - m_changed < save_delay)
			return;

		if (!Save(update_attempts))
			m_changed = now;
	};

	void KeyedStore::Flush()
	{
		if (m_dirty && !Save(flush_attempts))
			m_changed = std::chrono::steady_clock::now();
	};

	bool KeyedStore::Save(int attempts)
	{
		if (m_file.empty())
		{
			m_dirty = false;
			return true;
		};

		json document = json::object();

		for (const auto& [key, text] : m_texts)
			document[key] = text;

		const std::string text = document.dump(json_indent, '\t', false, json::error_handler_t::replace);

		if (!Files::WriteAtomic(m_file, text, { .attempts = attempts }))
			return false;

		m_dirty = false;

		return true;
	};
};
