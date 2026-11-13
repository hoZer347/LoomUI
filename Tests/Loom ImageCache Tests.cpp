#include "doctest.h"

#include "Testing/Headless.h"
#include "Testing/Load.h"

#include "ImageCache.h"
#include "Images.h"
#include "Text.h"

#include "imgui.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

using Loom::Testing::load_slack;

using Loom::ImageCache;

#pragma region Helpers

namespace
{
	constexpr auto wait_limit = std::chrono::seconds(20) * load_slack;
	constexpr auto wait_step = std::chrono::milliseconds(5);
	constexpr auto slow_load = std::chrono::milliseconds(200);
	constexpr auto settle = std::chrono::milliseconds(100);
	constexpr uint8_t grey = 128;
	constexpr uint8_t opaque = 255;
	constexpr int loaded_width = 6;
	constexpr int loaded_height = 4;
	constexpr int file_width = 9;
	constexpr int file_height = 3;
	constexpr const char* loaded_key = "loom:loaded";
	constexpr const char* empty_key = "loom:empty";
	constexpr const char* slow_key = "loom:slow";

	struct Context final
	{
		const Loom::Testing::Headless screen{ Loom::Testing::Headless::Options{ .buildFonts = false } };
	};

	struct Scratch final
	{
		Scratch() :
			path(std::filesystem::temp_directory_path() / "Loom ImageCache")
		{
			std::filesystem::create_directories(path);
		};

		~Scratch()
		{
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		};

		Scratch(const Scratch&) = delete;
		Scratch& operator=(const Scratch&) = delete;

		const std::filesystem::path path;
	};

	std::string Png(
		int width,
		int height)
	{
		Loom::Images::Pixels pixels;
		pixels.width = width;
		pixels.height = height;

		for (int index = 0; index < width * height; index++)
			pixels.rgba.insert(pixels.rgba.end(), { grey, grey, grey, opaque });

		const std::string bytes = Loom::Images::Encode(pixels, Loom::Images::Format::Png);

		REQUIRE(!bytes.empty());

		return bytes;
	};

	// Counts each key it is asked for, and supplies bytes only for the keys it was given.
	struct Loads final
	{
		ImageCache::Loader Loader()
		{
			return [this](const std::string& key, std::string& bytes)
			{
				if (key == slow_key)
					std::this_thread::sleep_for(slow_load);

				const std::lock_guard lock(m_mutex);
				m_asked[key]++;

				const auto found = m_supplied.find(key);

				if (found == m_supplied.end())
					return false;

				bytes = found->second;

				return true;
			};
		};

		void Supply(
			const std::string& key,
			const std::string& bytes)
		{
			const std::lock_guard lock(m_mutex);
			m_supplied[key] = bytes;
		};

		int Asked(const std::string& key)
		{
			const std::lock_guard lock(m_mutex);
			return m_asked[key];
		};

	private:
		std::mutex m_mutex;
		std::map<std::string, std::string> m_supplied;
		std::map<std::string, int> m_asked;
	};

	template<typename Done>
	bool Pump(
		ImageCache& cache,
		Done&& done)
	{
		const auto deadline = std::chrono::steady_clock::now() + wait_limit;

		while (!done() && std::chrono::steady_clock::now() < deadline)
		{
			cache.Update();
			std::this_thread::sleep_for(wait_step);
		};

		return done();
	};

	bool Settled(
		ImageCache& cache,
		const std::string& key)
	{
		return Pump(cache, [&cache, &key] { return cache.Find(key).state != ImageCache::State::Waiting; });
	};

	bool Reached(
		const std::atomic<int>& count,
		int wanted)
	{
		const auto deadline = std::chrono::steady_clock::now() + wait_limit;

		while (count < wanted && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(wait_step);

		return count >= wanted;
	};

	void Drop(const ImageCache::Entry& entry)
	{
		if (entry.texture != nullptr)
			entry.texture->Status = ImTextureStatus_Destroyed;
	};
};

#pragma endregion

TEST_SUITE("Loom ImageCache")
{
	TEST_CASE("a key the loader supplies measures from its bytes, with no file")
	{
		Loads loads;
		loads.Supply(loaded_key, Png(loaded_width, loaded_height));

		ImageCache cache({ }, loads.Loader());

		REQUIRE(Settled(cache, loaded_key));

		const ImageCache::Entry& entry = cache.Find(loaded_key);

		CHECK(entry.error.empty());
		REQUIRE(entry.state == ImageCache::State::Ready);
		CHECK(entry.width == loaded_width);
		CHECK(entry.height == loaded_height);
		CHECK(loads.Asked(loaded_key) == 1);
	};

	TEST_CASE("a key the loader passes on is read as a file path")
	{
		const Scratch scratch;
		const std::filesystem::path file = scratch.path / "shot.png";
		const std::string png = Png(file_width, file_height);

		{
			std::ofstream stream(file, std::ios::binary);
			stream.write(png.data(), (std::streamsize)png.size());
		};

		Loads loads;
		ImageCache cache({ }, loads.Loader());
		const std::string key = Loom::Text::PathUtf8(file);

		REQUIRE(Settled(cache, key));

		const ImageCache::Entry& entry = cache.Find(key);

		REQUIRE(entry.state == ImageCache::State::Ready);
		CHECK(entry.width == file_width);
		CHECK(entry.height == file_height);
		CHECK(loads.Asked(key) == 1);
	};

	TEST_CASE("with no loader every key is a file path, and a missing one fails by name")
	{
		const Scratch scratch;
		ImageCache cache;
		const std::string key = Loom::Text::PathUtf8(scratch.path / "gone.png");

		REQUIRE(Settled(cache, key));

		const ImageCache::Entry& entry = cache.Find(key);

		CHECK(entry.state == ImageCache::State::Failed);
		CHECK(entry.error == "Image not found: gone.png");
	};

	TEST_CASE("empty bytes from the loader fail as not an image, not as a missing file")
	{
		Loads loads;
		loads.Supply(empty_key, "");

		ImageCache cache({ }, loads.Loader());

		REQUIRE(Settled(cache, empty_key));

		const ImageCache::Entry& entry = cache.Find(empty_key);

		CHECK(entry.state == ImageCache::State::Failed);
		CHECK(entry.error == "Not an image.");
	};

	TEST_CASE("a measured key is cached, so finding it again asks the loader nothing")
	{
		Loads loads;
		loads.Supply(loaded_key, Png(loaded_width, loaded_height));

		ImageCache cache({ }, loads.Loader());

		REQUIRE(Settled(cache, loaded_key));

		const uint32_t changes = cache.Changes();

		for (int round = 0; round < 3; round++)
		{
			cache.Find(loaded_key);
			cache.Update();
		};

		std::this_thread::sleep_for(settle);
		cache.Update();

		CHECK(loads.Asked(loaded_key) == 1);
		CHECK(cache.Changes() == changes);
	};

	TEST_CASE("a used key decodes through the loader once, and its texture is kept")
	{
		const Context context;
		Loads loads;
		loads.Supply(loaded_key, Png(loaded_width, loaded_height));

		ImageCache cache({ }, loads.Loader());

		REQUIRE(Pump(cache, [&cache] { return cache.Use(loaded_key).texture != nullptr; }));

		ImTextureData* const texture = cache.Find(loaded_key).texture;

		CHECK(texture->Width == loaded_width);
		CHECK(texture->Height == loaded_height);
		CHECK(loads.Asked(loaded_key) == 2);

		for (int round = 0; round < 3; round++)
		{
			CHECK(cache.Use(loaded_key).texture == texture);
			cache.Update();
		};

		std::this_thread::sleep_for(settle);
		cache.Update();

		CHECK(loads.Asked(loaded_key) == 2);
		CHECK_FALSE(cache.Find(loaded_key).decoding);

		Drop(cache.Find(loaded_key));
	};

	TEST_CASE("each result counts as a change, failed or ready, and finished is called for each job")
	{
		Loads loads;
		loads.Supply(loaded_key, Png(loaded_width, loaded_height));
		loads.Supply(empty_key, "");

		std::atomic<int> finished = 0;
		ImageCache cache([&finished]() { ++finished; }, loads.Loader());
		const uint32_t start = cache.Changes();

		cache.Find(loaded_key);
		cache.Find(empty_key);

		REQUIRE(Settled(cache, loaded_key));
		REQUIRE(Settled(cache, empty_key));

		CHECK(Reached(finished, 2));
		CHECK(cache.Changes() == start + 2);
	};

	TEST_CASE("a cache let go while its loader is still running waits for it and leaves nothing behind")
	{
		Loads loads;
		loads.Supply(slow_key, Png(loaded_width, loaded_height));

		std::atomic<int> finished = 0;

		{
			ImageCache cache([&finished]() { ++finished; }, loads.Loader());

			cache.Find(slow_key);
			cache.Find(loaded_key);
		};

		CHECK(loads.Asked(slow_key) == 1);
		CHECK(loads.Asked(loaded_key) == 0);
		CHECK(finished == 1);
	};
};
