#include "ImageCache.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdint>
#include <cstring>
#include <utility>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL/gl.h>


namespace Loom
{
	namespace
	{
		constexpr int texture_side = 1536;
		constexpr auto unused_limit = std::chrono::seconds(30);

		// The OpenGL backend keeps nothing but the GL texture in TexID, so a texture can be freed
		// without a frame to run the backend's own destroy, which a library about to unload cannot wait for.
		void Discard(ImTextureData* texture)
		{
			if (texture->TexID != ImTextureID_Invalid && wglGetCurrentContext() != nullptr)
			{
				const GLuint id = (GLuint)(intptr_t)texture->TexID;
				glDeleteTextures(1, &id);
			};

			ImGui::GetPlatformIO().Textures.find_erase(texture);
			ImGui::UnregisterUserTexture(texture);
			IM_DELETE(texture);
		};
	};

	ImageCache::ImageCache(
		std::function<void()> finished,
		Loader loader) :
		m_finished(std::move(finished)),
		m_loader(std::move(loader))
	{
	};

	ImageCache::~ImageCache()
	{
		m_jobs.clear();

		if (m_running.valid())
			m_running.wait();

		if (ImGui::GetCurrentContext() == nullptr)
			return;

		for (std::pair<const std::string, Entry>& item : m_entries)
			Release(item.second);

		for (ImTextureData* const texture : m_dying)
			Discard(texture);
	};

	void ImageCache::Update()
	{
		if (m_result.valid() && m_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			Take(m_result.get());

		Start();

		const auto now = std::chrono::steady_clock::now();

		for (std::pair<const std::string, Entry>& item : m_entries)
			if (item.second.texture != nullptr && now - item.second.used > unused_limit)
				Release(item.second);

		Bury();
	};

	const ImageCache::Entry& ImageCache::Find(const std::string& path)
	{
		const auto [found, added] = m_entries.try_emplace(path);

		if (added)
		{
			m_jobs.push_back({ path, false });
			Start();
		};

		return found->second;
	};

	const ImageCache::Entry& ImageCache::Use(const std::string& path)
	{
		Find(path);

		Entry& entry = m_entries.at(path);
		entry.used = std::chrono::steady_clock::now();

		if (entry.state == State::Ready && entry.texture == nullptr && !entry.decoding)
		{
			entry.decoding = true;
			m_jobs.push_front({ path, true });
			Start();
		};

		return entry;
	};

	void ImageCache::Start()
	{
		if (m_result.valid()
			|| m_jobs.empty())
			return;

		std::promise<Finished> result;
		m_result = result.get_future();

		// The result is set before finished is called, so a loop woken by finished always finds it ready.
		m_running = std::async(
			std::launch::async,
			[job = std::move(m_jobs.front()), finished = m_finished, loader = m_loader, result = std::move(result)]() mutable
			{
				result.set_value(Run(std::move(job), loader));

				if (finished)
					finished();
			});

		m_jobs.pop_front();
	};

	ImageCache::Finished ImageCache::Run(Job job, const Loader& loader)
	{
		Finished finished;
		finished.path = std::move(job.path);
		finished.decode = job.decode;

		std::string bytes;

		if (loader && loader(finished.path, bytes))
		{
			finished.loaded = job.decode
				? Images::DecodeBytes(bytes, texture_side, finished.pixels, finished.error)
				: Images::MeasureBytes(bytes, finished.width, finished.height, finished.error);

			return finished;
		};

		const std::filesystem::path file = Images::Utf8Path(finished.path);

		finished.loaded = job.decode
			? Images::Decode(file, texture_side, finished.pixels, finished.error)
			: Images::Measure(file, finished.width, finished.height, finished.error);

		return finished;
	};

	void ImageCache::Take(Finished finished)
	{
		Entry& entry = m_entries[finished.path];

		if (finished.decode)
			entry.decoding = false;

		if (!finished.loaded)
		{
			entry.state = State::Failed;
			entry.error = std::move(finished.error);
			m_changes++;
			return;
		};

		if (!finished.decode)
		{
			entry.state = State::Ready;
			entry.width = finished.width;
			entry.height = finished.height;
			m_changes++;
			return;
		};

		ImTextureData* const texture = IM_NEW(ImTextureData)();
		texture->Create(ImTextureFormat_RGBA32, finished.pixels.width, finished.pixels.height);
		std::memcpy(texture->GetPixels(), finished.pixels.rgba.data(), finished.pixels.rgba.size());
		ImGui::RegisterUserTexture(texture);

		entry.texture = texture;
		entry.used = std::chrono::steady_clock::now();
	};

	void ImageCache::Release(Entry& entry)
	{
		if (entry.texture == nullptr)
			return;

		entry.texture->WantDestroyNextFrame = true;
		m_dying.push_back(std::exchange(entry.texture, nullptr));
	};

	void ImageCache::Bury()
	{
		std::erase_if(m_dying,
			[](ImTextureData* texture)
			{
				if (texture->Status != ImTextureStatus_Destroyed)
					return false;

				ImGui::UnregisterUserTexture(texture);
				IM_DELETE(texture);

				return true;
			});
	};
};
