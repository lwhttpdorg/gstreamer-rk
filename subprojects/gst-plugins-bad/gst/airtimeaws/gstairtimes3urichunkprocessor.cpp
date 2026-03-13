/*
 * airtime aws plugin
 * Copyright (C) 2025 mmhmm, Inc.
 *   @author: Teus Groenewoud <teus@mmhmm.app>
 *   @author: Tomasz Mikolajczyk <tomasz.mikolajczyk@mmhmm.app>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstairtimes3urichunkprocessor.hpp"
#include "gstairtimescopedhelpers.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <format>
#include <fstream>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <string_view>

#include <glib-object.h>
#include <gst/gst.h>
#include <json-glib/json-glib.h>

namespace gst::airtime
{

    namespace
    {

        constexpr std::string_view file_chunk_extension = ".part";
        constexpr std::string_view metadata_file_name = "metadata.json";
        constexpr std::string_view file_chunk_size_file_name = "file_chunk_size.txt";
        constexpr std::string_view last_access_time_file_name = "last_access_time.txt";
        constexpr std::string_view dir_in_use_file_name = "dir_in_use.empty";

    } // namespace

    // ----------------------------------------------------------------------------------------------------------------- //

    LRUCacheEvictionPolicy::LRUCacheEvictionPolicy(std::uint64_t max_cache_size) : max_cache_size_(max_cache_size)
    {
        assert(max_cache_size_ > 0 and "Max cache size must be greater than zero");
    }

    std::uint64_t LRUCacheEvictionPolicy::getAvailableSize(const std::vector<S3URICacheManagerEntry> &entries) const
    {
        const auto in_use_total_size =
            std::accumulate(entries.begin(), entries.end(), std::uint64_t{0},
                            [](std::uint64_t in_use_size, const S3URICacheManagerEntry &entry)
                            {
                                if (std::filesystem::exists(entry.cache_key_directory / dir_in_use_file_name))
                                {
                                    return in_use_size + entry.metadata.content_length;
                                }
                                return in_use_size;
                            });
        return max_cache_size_ - std::min(max_cache_size_, in_use_total_size);
    }

    std::uint64_t LRUCacheEvictionPolicy::examineEntries(const S3URIObjectMetadata &metadata,
                                                         std::vector<S3URICacheManagerEntry> &entries,
                                                         std::uint64_t current_cache_size)
    {
        auto entries_indices = getLRUSortedAndUnusedEntriesIndices(entries);

        bool has_enough_space = current_cache_size + metadata.content_length <= max_cache_size_;
        if (not has_enough_space)
        {
            for (std::size_t index : entries_indices)
            {
                // Evict the entry
                current_cache_size -= std::min(entries[index].metadata.content_length, current_cache_size);
                std::filesystem::remove_all(entries[index].cache_key_directory);
                entries.erase(entries.begin() + index);

                has_enough_space = current_cache_size + metadata.content_length <= max_cache_size_;
                if (has_enough_space)
                {
                    break;
                }
            }
        }
        if (not has_enough_space)
        {
            // If we still don't have enough space, we need to evict more entries
            throw std::runtime_error(std::format("Not enough space in cache after eviction, current size: {}, max size: "
                                                 "{}. Consider increasing the cache size.",
                                                 current_cache_size, max_cache_size_));
        }
        return current_cache_size;
    }

    std::vector<std::size_t>
    LRUCacheEvictionPolicy::getLRUSortedAndUnusedEntriesIndices(const std::vector<S3URICacheManagerEntry> &entries) const
    {
        std::vector<std::size_t> entries_indices; // Indices of entries sorted by last access time
        entries_indices.resize(entries.size());
        std::iota(entries_indices.begin(), entries_indices.end(), 0);

        std::sort(entries_indices.begin(), entries_indices.end(), [&entries](std::size_t lhs, std::size_t rhs)
                  { return entries[lhs].last_access_time < entries[rhs].last_access_time; });
        // entries_indices now contains indices of entries sorted by last access time

        // remove entries_indices that points to entries that are currently in use
        entries_indices.erase(std::remove_if(entries_indices.begin(), entries_indices.end(),
                                             [&entries](std::size_t index)
                                             {
                                                 return std::filesystem::exists(entries[index].cache_key_directory /
                                                                                dir_in_use_file_name);
                                             }),
                              entries_indices.end());
        return entries_indices;
    }

    // ----------------------------------------------------------------------------------------------------------------- //

    S3URICacheManager::S3URICacheManager(std::filesystem::path cache_base_directory,
                                         std::unique_ptr<S3URICacheEvictionPolicy> eviction_policy) : cache_base_directory_{std::move(cache_base_directory)},
                                                                                                      eviction_policy_{std::move(eviction_policy)}
    {
        loadCurrentCacheState();
    }

    void S3URICacheManager::loadCurrentCacheState()
    {
        std::lock_guard lock{entries_access_};
        entries_.clear();
        current_size_ = 0;
        if (not std::filesystem::is_directory(cache_base_directory_))
        {
            return;
        }
        for (auto const &dir_entry : std::filesystem::recursive_directory_iterator(cache_base_directory_))
        {
            if (std::filesystem::is_regular_file(dir_entry.path()) && dir_entry.path().filename() == metadata_file_name)
            {
                try
                {
                    auto metadata = deserializeS3URIObjectMetadataFromJSON(dir_entry.path());
                    const std::time_t last_access_time =
                        loadLastAccessTimeFromFileOrCurrent(dir_entry.path().parent_path() / last_access_time_file_name);
                    const auto &entry =
                        entries_.emplace_back(S3URICacheManagerEntry{.cache_key_directory = dir_entry.path().parent_path(),
                                                                     .metadata = std::move(metadata),
                                                                     .last_access_time = last_access_time});
                    current_size_ += entry.metadata.content_length;
                }
                catch (const std::exception &e)
                {
                    GST_ERROR("Failed to deserialize metadata from JSON: %s", e.what());
                    continue; // Skip this entry if deserialization fails
                }
            }
        }
    }

    bool S3URICacheManager::isEnoughSpaceForKey(const S3URIObjectMetadata &metadata) const
    {
        std::lock_guard lock{entries_access_};

        // Check if the available cache size is big enough for the new metadata's content length
        if (eviction_policy_->getAvailableSize(entries_) == S3URICacheEvictionPolicy::unlimitedCacheSize)
        {
            // No limit on cache size, always return true
            return true;
        }
        const auto available_size = eviction_policy_->getAvailableSize(entries_);
        if (available_size < metadata.content_length)
        {
            GST_DEBUG("Not enough space for key: %s/%s, content length: %" G_GUINT64_FORMAT
                      ", available size: %" G_GUINT64_FORMAT,
                      metadata.bucket.data(), metadata.key.data(), metadata.content_length, available_size);
            return false;
        }
        return true;
    }

    void S3URICacheManager::keyRequested(const S3URIObjectMetadata &metadata)
    {
        std::lock_guard lock{entries_access_};

        const S3URICacheManagerEntry *cache_entry = nullptr;
        auto entry_it = std::find_if(entries_.begin(), entries_.end(), [&metadata](const S3URICacheManagerEntry &entry)
                                     { return entry.metadata.bucket == metadata.bucket and entry.metadata.key == metadata.key; });
        if (entry_it != entries_.end())
        {
            // Key already exists, update last access time
            entry_it->last_access_time = std::time(nullptr);
            cache_entry = &*entry_it;
            GST_DEBUG("Key already exists in cache, updated last access time.");
        }
        else
        {
            // New key, add to cache
            std::string_view key_view = metadata.key;
            if (key_view.starts_with("/"))
            {
                // Remove leading slash from s3_key
                key_view.remove_prefix(1);
            }

            current_size_ = eviction_policy_->examineEntries(metadata, entries_, current_size_);

            const auto &entry = entries_.emplace_back(
                S3URICacheManagerEntry{.cache_key_directory = cache_base_directory_ / metadata.bucket / key_view,
                                       .metadata = metadata,
                                       .last_access_time = std::time(nullptr)});
            cache_entry = &entry;
            current_size_ += metadata.content_length;
            GST_DEBUG("New key added to cache: %s/%s", metadata.bucket.data(), metadata.key.data());
        }

        assert(cache_entry);
        saveLastAccessTimeToFile(cache_entry->cache_key_directory / last_access_time_file_name,
                                 cache_entry->last_access_time);
    }

    void S3URICacheManager::keyRemoved(const S3URIObjectMetadata &metadata)
    {
        std::lock_guard lock{entries_access_};

        auto entry_it = std::find_if(entries_.begin(), entries_.end(), [&](const S3URICacheManagerEntry &entry)
                                     { return entry.metadata.bucket == metadata.bucket and entry.metadata.key == metadata.key; });
        if (entry_it != entries_.end())
        {
            // Key exists, remove it
            current_size_ -= entry_it->metadata.content_length;
            entries_.erase(entry_it);
            GST_DEBUG("Key removed from cache: %s/%s", entry_it->metadata.bucket.data(), entry_it->metadata.key.data());
        }
        else
        {
            GST_DEBUG("Key not found in cache: %s/%s", metadata.bucket.data(), metadata.key.data());
        }
    }

    std::uint64_t S3URICacheManager::currentCacheSize() const
    {
        std::lock_guard lock{entries_access_};
        return current_size_;
    }

    // -----------------------------------------------------------------------------------------------------------------

    CachingS3URIChunkProcessor::CachingS3URIChunkProcessor(std::shared_ptr<S3URICacheManager> cache_manager,
                                                           std::string_view s3_bucket, std::string_view s3_key,
                                                           std::uint64_t file_chunk_standard_size, bool trust_cached_data) : cache_manager_{std::move(cache_manager)},
                                                                                                                             directory_path_{cache_manager_->cacheBaseDirectory()},
                                                                                                                             file_chunk_standard_size_{file_chunk_standard_size},
                                                                                                                             trust_cached_data_{trust_cached_data}
    {
        if (s3_key.starts_with("/"))
        {
            // Remove leading slash from s3_key
            s3_key.remove_prefix(1);
        }

        directory_path_ /= s3_bucket;
        directory_path_ /= s3_key;
        GST_DEBUG("Using cache directory: %s", directory_path_.string().c_str());
        std::filesystem::create_directories(directory_path_);
        createDirectoryInUseFile();
        loadChunksFromDirectory();
        readCachedMetadataIfExists();

        if (trust_cached_data_ and metadata_.has_value())
        {
            fillGaps();
        }

        saveFileChunkSizeToFileIfNotExists(directory_path_ / file_chunk_size_file_name, file_chunk_standard_size_);
    }

    CachingS3URIChunkProcessor::~CachingS3URIChunkProcessor()
    {
        try
        {
            std::lock_guard lock{state_access_};
            removeDirectoryInUseFile();

            // Remove all files in the directory. That may happen if the cache was not in a consistent state.
            if (recreate_cache_content_)
            {
                resetCacheDirectory(false);
            }
        }
        catch (const std::exception &e)
        {
            GST_ERROR("Unexpected exception: %s", e.what());
        }
    }

    bool CachingS3URIChunkProcessor::needsObjectMetadata() const
    {
        return trust_cached_data_ ? not cached_metadata_.has_value() and not metadata_.has_value()
                                  : not metadata_.has_value();
    }

    void CachingS3URIChunkProcessor::setObjectMetadata(S3URIObjectMetadata metadata)
    {
        if (metadata.entity_tag.empty())
        {
            throw std::invalid_argument("MD5 checksum cannot be empty");
        }

        if (not cache_manager_->isEnoughSpaceForKey(metadata))
        {
            throw std::runtime_error(
                std::format("Not enough space in cache for key: {} / {}. Consider increasing cache size.", metadata.bucket,
                            metadata.key));
        }

        std::lock_guard lock{state_access_};
        metadata_ = std::move(metadata);
        fillGaps();
    }

    void CachingS3URIChunkProcessor::fillGaps()
    {
        assert(metadata_.has_value());
        // It may happen that loadChunksFromDirectory loaded non-contiguous chunks and we need to fill the gaps here.
        const std::uint64_t expected_file_chunks_count =
            calculateExpectedCacheFilesCount(metadata_->content_length, file_chunk_standard_size_);
        if (file_chunks_.empty())
        {
            // nothing is loaded from the cache directory yet, so we can just resize the vector
            file_chunks_.resize(expected_file_chunks_count);
        }
        else
        {
            // NOTE: Some file chunks are being loaded from the cache directory, so we need to fill the gaps with empty
            // CacheFileChunk.
            // The no_gaps_file_chunks_size value must reflect the greatest index of the file_chunks_ vector as it may
            // happen that the cache dir contained more chunks than expected and we still need to handle that case
            // correctly.
            const std::uint64_t no_gaps_file_chunks_size =
                std::max(expected_file_chunks_count, file_chunks_.back().index + 1);
            std::vector<CacheFileChunk> no_gaps_file_chunks_;
            no_gaps_file_chunks_.resize(no_gaps_file_chunks_size);
            for (auto &file_chunk : file_chunks_)
            {
                no_gaps_file_chunks_[file_chunk.index] = std::move(file_chunk);
            }
            file_chunks_ = std::move(no_gaps_file_chunks_);
        }
    }

    S3URIObjectMetadata CachingS3URIChunkProcessor::getObjectMetadata() const
    {
        std::lock_guard lock{state_access_};
        if (not metadata_)
        {
            throw std::runtime_error("Metadata has not been set");
        }
        return *metadata_;
    }

    std::pair<bool, std::vector<S3URIFileChunkGapSpec>> CachingS3URIChunkProcessor::needsChunks() const
    {
        std::lock_guard lock{state_access_};
        if (not cached_metadata_.has_value())
        {
            recreate_cache_content_ = true;
            return {true, {}};
        }

        if (not metadata_.has_value())
        {
            recreate_cache_content_ = true;
            return {true, {}};
        }

        if (cached_metadata_ != metadata_)
        {
            GST_DEBUG("Metadata values do not match, need to download all chunks.");
            recreate_cache_content_ = true;
            return {true, {}};
        }

        const auto file_chunk_size_file = directory_path_ / file_chunk_size_file_name;
        std::uint64_t deser_file_chunk_standard_size = 0;
        try
        {
            deser_file_chunk_standard_size = loadFileChunkSizeFromFile(file_chunk_size_file);
        }
        catch (const std::exception &e)
        {
            GST_DEBUG("Unable to load file chunk size file: %s.", e.what());
            recreate_cache_content_ = true;
            return {true, {}};
        }
        if (deser_file_chunk_standard_size != file_chunk_standard_size_)
        {
            GST_DEBUG("File chunk standard size does not match, need to download all chunks.");
            recreate_cache_content_ = true;
            return {true, {}};
        }

        // it may happen that loadChunksFromDirectory loaded more files than expected
        const auto expected_files_count =
            calculateExpectedCacheFilesCount(metadata_->content_length, file_chunk_standard_size_);
        if (file_chunks_.size() > expected_files_count)
        {
            recreate_cache_content_ = true;
            return {true, {}};
        }

        if (file_chunks_content_length_ > metadata_->content_length)
        {
            GST_DEBUG("Content length of file chunks is too big, need to download chunks.");
            recreate_cache_content_ = true;
            return {true, {}};
        }

        auto calc_gaps = calculateChunkGapsImpl();
        if (file_chunks_content_length_ == metadata_->content_length and not calc_gaps.gaps.empty())
        {
            GST_DEBUG(
                "Filling the gaps would cause the content length to exceed the expected length, need to download chunks.");
            recreate_cache_content_ = true;
            return {true, {}};
        }
        recreate_cache_content_ = false;
        if (calc_gaps.full_range)
        {
            assert(calc_gaps.gaps.empty() and "If full range is true, gaps must be empty");
            return {true, {}};
        }
        GST_DEBUG("Cached file chunks from previous run are valid and can be reused.");
        if (calc_gaps.gaps.empty())
        {
            // No gaps, so we can return false to indicate that no chunks are needed and the S3 key is fully restored
            // from the cache.
            // However, we still need to notify the cache manager that the key was requested
            // to update the last access time of the cache entry.
            cache_manager_->keyRequested(*metadata_);
        }
        return {not calc_gaps.gaps.empty(), std::move(calc_gaps.gaps)};
    }

    void CachingS3URIChunkProcessor::processChunk(const S3URIChunkSpec &chunk_spec, std::istream &chunk_body)
    {
        // NOTE: This implementation has a limitation that it assumes the downloaded chunk size is always a multiple of
        // file_chunk_standard_size_.
        std::lock_guard lock{state_access_};

        if (not metadata_)
        {
            throw std::runtime_error("Metadata has not been set");
        }

        if (chunk_spec.standardSize() % file_chunk_standard_size_ != 0)
        {
            throw std::invalid_argument{
                std::format("Incoming chunk standard size {} is not a multiple of file chunk standard size {}",
                            chunk_spec.standardSize(), file_chunk_standard_size_)};
        }

        if (first_incoming_chunk_)
        {
            if (recreate_cache_content_)
            {
                resetCacheDirectory(true);
                recreate_cache_content_ = false;
            }
            cache_manager_->keyRequested(*metadata_);
            GST_INFO("New cache size: %" G_GUINT64_FORMAT, cache_manager_->currentCacheSize());
            first_incoming_chunk_ = false;
        }

        // divide into chunk files
        auto remain = chunk_spec.actualSize();
        // first file chunk index. If chunk size is greater that file chunk size there will be more files created
        auto chunk_file_index = chunk_spec.index() * chunk_spec.standardSize() / file_chunk_standard_size_;
        std::istreambuf_iterator<char> chunk_body_iterator(chunk_body);
        while (remain > 0)
        {
            if (chunk_file_index >= file_chunks_.size())
            {
                throw std::runtime_error("Unexpectedly large incoming chunk size");
            }
            // calculate the size of the chunk to write
            const auto write_size = std::min<std::size_t>(file_chunk_standard_size_, remain);

            if (not file_chunks_[chunk_file_index].file_path.empty())
            {
                // NOTE: this chunk_spec has been downloaded to fill the file chunk gap, but due to its size it concerns
                // also other file chunks, so we need to check if the file chunk is already present and skip writing it
                // again unncessarily.
                GST_DEBUG("Chunk file %s already exists, skipping re-writing",
                          file_chunks_[chunk_file_index].file_path.c_str());
                remain -= write_size;
                chunk_file_index++;
                continue;
            }

            // create a new chunk file path
            std::filesystem::path chunk_file_path =
                directory_path_ / std::format("chunk_{:08}{}", chunk_file_index, file_chunk_extension);

            // open a filestream to write the chunk data
            std::ofstream chunk_file(chunk_file_path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (not chunk_file.is_open())
            {
                throw std::runtime_error{std::format("Unable to open file for writing: {}", chunk_file_path.string())};
            }

            // Copy the chunk data from the input to the output file.
            // Note: std::copy_n will not advance the input iterator at the end, so before the next iteration we need to
            // increment it to point to the next character.
            std::copy_n(chunk_body_iterator, write_size, std::ostreambuf_iterator<char>(chunk_file));
            if (chunk_body_iterator == std::istreambuf_iterator<char>())
            {
                break;
            }
            ++chunk_body_iterator; // advance the iterator so it starts pointing to the next byte
            file_chunks_[chunk_file_index] =
                CacheFileChunk{.file_path = chunk_file_path, .content_length = write_size, .index = chunk_file_index};
            GST_DEBUG("download chunk complete: %" G_GSIZE_FORMAT " - file: %s", chunk_spec.index(),
                      chunk_file_path.c_str());

            // update remaining bytes and increment the chunk file index
            remain -= write_size;
            chunk_file_index++;
            file_chunks_content_length_ += write_size;
        }
    }

    bool CachingS3URIChunkProcessor::allChunksProcessed()
    {
        std::lock_guard lock{state_access_};
        if (not metadata_)
        {
            throw std::runtime_error("Metadata has not been set");
        }
        if (file_chunks_content_length_ != metadata_->content_length)
        {
            GST_ERROR("File chunks content length %" G_GUINT64_FORMAT
                      " does not match metadata content length %" G_GUINT64_FORMAT,
                      file_chunks_content_length_, metadata_->content_length);
            recreate_cache_content_ = true;
            return false;
        }
        return true;
    }

    std::pair<S3BufferFillStatus, std::uint64_t> CachingS3URIChunkProcessor::fill(std::uint8_t *data, std::uint64_t offset,
                                                                                  std::uint64_t size)
    {
        std::lock_guard lock{state_access_};
        std::uint64_t chunk_index = offset / file_chunk_standard_size_;
        std::uint64_t pos_in_chunk = offset % file_chunk_standard_size_;

        std::uint64_t remain{size};
        std::uint64_t bytes_read{0};

        while (remain > 0 and chunk_index < file_chunks_.size())
        {
            CacheFileChunk *cache_chunk = &file_chunks_[chunk_index];

            if (pos_in_chunk >= cache_chunk->content_length)
            {
                // we've tried to read past the end of the chunk
                throw std::runtime_error{std::format("Position {} in chunk {} exceeds its content length {}", pos_in_chunk,
                                                     chunk_index, cache_chunk->content_length)};
            }

            // open file
            std::ifstream chunk_file(cache_chunk->file_path, std::ios::in | std::ios::binary);
            if (not chunk_file.is_open())
            {
                throw std::runtime_error{
                    std::format("Unable to open file for reading: {}", cache_chunk->file_path.string())};
            }

            // seek
            chunk_file.seekg(pos_in_chunk, std::ios::beg);
            // Check if the seek operation was successful
            if (not chunk_file)
            {
                throw std::runtime_error{
                    std::format("Failed to seek to position {} in file {}", pos_in_chunk, cache_chunk->file_path.string())};
            }

            const std::streampos current_pos = chunk_file.tellg();
            if (current_pos != static_cast<std::streampos>(pos_in_chunk))
            {
                throw std::runtime_error{
                    std::format("Seek operation failed for file: {}. Requested position {}, but got {}",
                                cache_chunk->file_path.string(), pos_in_chunk, static_cast<std::size_t>(current_pos))};
            }

            // read
            const std::uint64_t read_size = std::min<std::uint64_t>(file_chunk_standard_size_ - pos_in_chunk, remain);
            chunk_file.read(reinterpret_cast<char *>(data + bytes_read), read_size);
            if (chunk_file.bad() || (chunk_file.fail() && not chunk_file.eof()))
            {
                throw std::runtime_error{
                    std::format("Unable to read {} bytes from chunk file: {}", read_size, cache_chunk->file_path.string())};
            }
            const std::size_t gcount = static_cast<std::size_t>(chunk_file.gcount());

            if (gcount < read_size and not chunk_file.eof())
            {
                throw std::runtime_error{
                    std::format("Unexpected read size from chunk file: expected {}, got {}", read_size, gcount)};
            }

            remain -= gcount;
            chunk_index++;
            pos_in_chunk = 0;
            bytes_read += gcount;
        }
        return {(remain == 0) ? S3BufferFillStatus::success : S3BufferFillStatus::end_of_file, bytes_read};
    }

    S3URIFileChunkGaps CachingS3URIChunkProcessor::calculateChunkGaps() const
    {
        std::lock_guard lock{state_access_};
        return calculateChunkGapsImpl();
    }

    void CachingS3URIChunkProcessor::createDirectoryInUseFile()
    {
        std::ofstream dir_in_use_file{directory_path_ / dir_in_use_file_name, std::ios::out | std::ios::trunc};
    }

    void CachingS3URIChunkProcessor::removeDirectoryInUseFile()
    {
        std::filesystem::remove(directory_path_ / dir_in_use_file_name);
    }

    void CachingS3URIChunkProcessor::loadChunksFromDirectory()
    {
        std::lock_guard lock{state_access_};
        // get sorted list of file chunks
        for (const auto &entry : std::filesystem::directory_iterator(directory_path_))
        {
            if (std::filesystem::is_regular_file(entry.path()) && entry.path().extension() == file_chunk_extension)
            {
                const auto index = extractIndexFromChunkFileName(entry.path());
                const auto file_size = static_cast<std::size_t>(entry.file_size());
                file_chunks_.emplace_back(
                    CacheFileChunk{.file_path = entry.path(), .content_length = file_size, .index = index});
                file_chunks_content_length_ += file_size;
            }
        }

        std::sort(file_chunks_.begin(), file_chunks_.end(),
                  [](const auto &lhs, const auto &rhs)
                  { return lhs.index < rhs.index; });
    }

    void CachingS3URIChunkProcessor::readCachedMetadataIfExists()
    {
        std::lock_guard lock{state_access_};
        const auto metadata_file = directory_path_ / metadata_file_name;
        try
        {
            cached_metadata_ = deserializeS3URIObjectMetadataFromJSON(metadata_file);
            if (trust_cached_data_)
            {
                metadata_ = cached_metadata_;
            }
        }
        catch (const std::exception &e)
        {
            GST_DEBUG("Unable to deserialize metadata json file: %s.", e.what());
        }
    }

    void CachingS3URIChunkProcessor::resetCacheDirectory(bool recreate_cache_content)
    {
        // remove the existing cache directory, so that we can start fresh
        std::filesystem::remove_all(directory_path_);

        if (not metadata_)
        {
            return;
        }
        cache_manager_->keyRemoved(*metadata_);

        if (recreate_cache_content)
        {
            std::filesystem::create_directories(directory_path_);

            // serialize the metadata to a file
            const auto metadata_file = directory_path_ / metadata_file_name;
            serializeS3URIObjectMetadataToJSON(*metadata_, metadata_file);
            cached_metadata_ = metadata_;

            const auto file_chunk_size_file = directory_path_ / file_chunk_size_file_name;
            saveFileChunkSizeToFile(file_chunk_size_file, file_chunk_standard_size_);

            file_chunks_content_length_ = 0;
            first_incoming_chunk_ = true;

            const std::size_t expected_file_chunks_count =
                calculateExpectedCacheFilesCount(metadata_->content_length, file_chunk_standard_size_);
            file_chunks_.clear();
            file_chunks_.resize(expected_file_chunks_count);
        }
    }

    S3URIFileChunkGaps CachingS3URIChunkProcessor::calculateChunkGapsImpl() const
    {
        if (file_chunks_.empty())
        {
            return full_range_gaps;
        }

        std::vector<S3URIFileChunkGapSpec> missing_indices;

        // The non-gap element contains file_path set, while gap does not.
        auto find_next_non_gap = [this](auto from_it)
        {
            return std::find_if(from_it, file_chunks_.end(),
                                [](const auto &file_chunk_entry)
                                { return not file_chunk_entry.file_path.empty(); });
        };

        auto process_gap = [this, &missing_indices](auto non_gap_it, auto next_non_gap_it)
        {
            const auto gap_distance = std::distance(non_gap_it, next_non_gap_it);
            const auto gap_index = static_cast<std::uint64_t>(std::distance(file_chunks_.begin(), non_gap_it));
            if (gap_distance == 1)
            {
                missing_indices.emplace_back(S3URIFileChunkGapIndex{gap_index});
            }
            else if (gap_distance > 1)
            {
                missing_indices.emplace_back(S3URIFileChunkGapIndicesRange{gap_index, gap_index + gap_distance - 1});
            }
        };

        auto non_gap_it = file_chunks_.begin();
        while (non_gap_it != file_chunks_.end())
        {
            auto next_non_gap_it = find_next_non_gap(non_gap_it);
            if (next_non_gap_it != file_chunks_.end())
            {
                process_gap(non_gap_it, next_non_gap_it);
                non_gap_it = ++next_non_gap_it;
            }
            else
            {
                break;
            }
        }

        if (non_gap_it == file_chunks_.begin())
        {
            // no non-GAPs found - return an empty vector. Without this check we would return a single full-range.
            return full_range_gaps;
        }
        else if (non_gap_it != file_chunks_.end())
        {
            process_gap(non_gap_it, file_chunks_.end());
        }
        return S3URIFileChunkGaps{.full_range = false, .gaps = std::move(missing_indices)};
    }

    // -----------------------------------------------------------------------------------------------------------------

    std::uint64_t extractIndexFromChunkFileName(const std::filesystem::path &chunk_file_path)
    {
        const std::string filename = chunk_file_path.filename().string();
        const std::string_view prefix = "chunk_";
        if (filename.starts_with(prefix) and filename.ends_with(file_chunk_extension))
        {
            std::string_view filename_view{filename};
            filename_view.remove_prefix(prefix.size());
            filename_view.remove_suffix(file_chunk_extension.size());
            std::uint64_t index{};
            const std::from_chars_result result =
                std::from_chars(filename_view.data(), filename_view.data() + filename_view.size(), index);
            if (result.ec == std::errc{} and result.ptr == filename_view.data() + filename_view.size())
            {
                return index;
            }
        }
        throw std::runtime_error("Invalid chunk file name format: " + chunk_file_path.string());
    }

    void serializeS3URIObjectMetadataToJSON(const S3URIObjectMetadata &metadata, std::ostream &json_output_stream)
    {
        ScopedJsonBuilder builder{json_builder_new()};
        json_builder_begin_object(builder.get());

        json_builder_set_member_name(builder.get(), "bucket");
        json_builder_add_string_value(builder.get(), metadata.bucket.c_str());

        json_builder_set_member_name(builder.get(), "key");
        json_builder_add_string_value(builder.get(), metadata.key.c_str());

        json_builder_set_member_name(builder.get(), "content_length");
        json_builder_add_int_value(builder.get(), static_cast<std::int64_t>(metadata.content_length));

        json_builder_set_member_name(builder.get(), "entity_tag");
        json_builder_add_string_value(builder.get(), metadata.entity_tag.c_str());

        json_builder_set_member_name(builder.get(), "version_id");
        json_builder_add_string_value(builder.get(), metadata.version_id.c_str());

        json_builder_set_member_name(builder.get(), "expiration");
        json_builder_add_string_value(builder.get(), metadata.expiration.c_str());

        json_builder_set_member_name(builder.get(), "last_modified");
        json_builder_add_string_value(builder.get(), metadata.last_modified.c_str());

        json_builder_end_object(builder.get());

        ScopedJsonNode root{json_builder_get_root(builder.get())};
        ScopedJsonGenerator generator{json_generator_new()};
        json_generator_set_root(generator.get(), root.get());
        json_generator_set_pretty(generator.get(), TRUE); // pretty print the JSON output

        ScopedGChar json_str{json_generator_to_data(generator.get(), NULL)};
        json_output_stream << json_str.get();
    }

    void serializeS3URIObjectMetadataToJSON(const S3URIObjectMetadata &metadata, const std::filesystem::path &metadata_file)
    {
        std::ofstream metadata_file_stream{metadata_file};
        serializeS3URIObjectMetadataToJSON(metadata, metadata_file_stream);
        if (not metadata_file_stream)
        {
            throw std::runtime_error{std::format("Failed to write metadata to file: {}", metadata_file.string())};
        }
    }

    namespace
    {

        std::string getRequiredStringNodeValue(JsonObject *json_object, const char *node_name)
        {
            if (json_object_has_member(json_object, node_name))
            {
                JsonNode *node = json_object_get_member(json_object, node_name);
                if (json_node_get_value_type(node) == G_TYPE_STRING)
                {
                    const char *node_value = json_object_get_string_member(json_object, node_name);
                    return node_value ? node_value : "";
                }
                throw std::runtime_error(std::format("JSON field '{}' is not a string", node_name));
            }
            throw std::runtime_error(std::format("JSON missing required field: {}", node_name));
        }

        int64_t getRequiredInt64NodeValue(JsonObject *json_object, const char *node_name)
        {
            if (json_object_has_member(json_object, node_name))
            {
                JsonNode *node = json_object_get_member(json_object, node_name);
                if (json_node_get_value_type(node) == G_TYPE_INT64)
                {
                    return json_object_get_int_member(json_object, node_name);
                }
                throw std::runtime_error(std::format("JSON field '{}' is not an integer", node_name));
            }
            throw std::runtime_error(std::format("JSON missing required field: {}", node_name));
        }

    } // namespace

    S3URIObjectMetadata deserializeS3URIObjectMetadataFromJSON(std::istream &json_input_stream)
    {
        S3URIObjectMetadata metadata;

        std::string json_str{std::istreambuf_iterator<char>{json_input_stream}, std::istreambuf_iterator<char>{}};

        ScopedJsonParser parser{json_parser_new()};
        GError *error = NULL;

        if (not json_parser_load_from_data(parser.get(), json_str.c_str(), -1, &error))
        {
            ScopedGError scoped_error{error};
            throw std::runtime_error(std::format("Failed to parse JSON data: {}", error->message));
        }

        JsonNode *root = json_parser_get_root(parser.get());
        if (not root)
        {
            throw std::runtime_error("Failed to get root JSON node");
        }

        if (json_node_get_node_type(root) != JSON_NODE_OBJECT)
        {
            throw std::runtime_error("Invalid JSON format: root is not an object");
        }

        JsonObject *json_object = json_node_get_object(root);
        if (not json_object)
        {
            throw std::runtime_error("Failed to get JSON object from root node");
        }

        metadata.bucket = getRequiredStringNodeValue(json_object, "bucket");
        metadata.key = getRequiredStringNodeValue(json_object, "key");
        metadata.content_length = static_cast<std::size_t>(getRequiredInt64NodeValue(json_object, "content_length"));
        metadata.entity_tag = getRequiredStringNodeValue(json_object, "entity_tag");
        metadata.version_id = getRequiredStringNodeValue(json_object, "version_id");
        metadata.expiration = getRequiredStringNodeValue(json_object, "expiration");
        metadata.last_modified = getRequiredStringNodeValue(json_object, "last_modified");
        return metadata;
    }

    S3URIObjectMetadata deserializeS3URIObjectMetadataFromJSON(const std::filesystem::path &metadata_file)
    {
        std::ifstream metadata_file_stream{metadata_file};
        if (not metadata_file_stream.is_open())
        {
            throw std::runtime_error{std::format("Failed to open metadata file: {}", metadata_file.string())};
        }
        return deserializeS3URIObjectMetadataFromJSON(metadata_file_stream);
    }

    namespace
    {

        template <typename T>
        void saveInfoToFile(const std::filesystem::path &location, T info)
        {
            std::ofstream stream{location, std::ios::out | std::ios::trunc};
            if (not stream.is_open())
            {
                throw std::runtime_error{std::format("Failed to open file for writing: {}", location.string())};
            }
            stream << info;
            if (not stream)
            {
                throw std::runtime_error{std::format("Failed to write to file: {}", location.string())};
            }
        }

        template <typename T>
        T loadInfoFromFile(const std::filesystem::path &location)
        {
            T info{};
            std::ifstream stream{location};
            if (stream.is_open())
            {
                stream >> info;
                if (not stream)
                {
                    throw std::runtime_error{std::format("Failed to read from file: {}", location.string())};
                }
                return info;
            }
            throw std::runtime_error{std::format("Failed to open file for reading: {}", location.string())};
        }

    } // namespace

    void saveLastAccessTimeToFile(const std::filesystem::path &last_access_time_file, std::time_t last_access_time)
    {
        saveInfoToFile(last_access_time_file, last_access_time);
        GST_DEBUG("Last access time saved to file: %s", last_access_time_file.string().c_str());
    }

    std::time_t loadLastAccessTimeFromFile(const std::filesystem::path &last_access_time_file)
    {
        return loadInfoFromFile<std::time_t>(last_access_time_file);
    }

    std::time_t loadLastAccessTimeFromFileOrCurrent(const std::filesystem::path &last_access_time_file)
    {
        if (std::filesystem::exists(last_access_time_file))
        {
            return loadLastAccessTimeFromFile(last_access_time_file);
        }
        return std::time(nullptr);
    }

    void saveFileChunkSizeToFile(const std::filesystem::path &file_chunk_size_file, std::uint64_t file_chunk_size)
    {
        saveInfoToFile(file_chunk_size_file, file_chunk_size);
        GST_DEBUG("File chunk size saved to file: %s", file_chunk_size_file.string().c_str());
    }

    void saveFileChunkSizeToFileIfNotExists(const std::filesystem::path &file_chunk_size_file,
                                            std::uint64_t file_chunk_size)
    {
        if (not std::filesystem::exists(file_chunk_size_file))
        {
            saveFileChunkSizeToFile(file_chunk_size_file, file_chunk_size);
        }
    }

    std::uint64_t loadFileChunkSizeFromFile(const std::filesystem::path &file_chunk_size_file)
    {
        return loadInfoFromFile<std::uint64_t>(file_chunk_size_file);
    }

    std::uint64_t calculateTotalGapsNumber(const std::vector<S3URIFileChunkGapSpec> &gaps)
    {
        return std::accumulate(gaps.begin(), gaps.end(), std::size_t{0}, [](auto acc, const auto &gap)
                               { return std::visit(
                                     [&acc](const auto &gap_spec)
                                     {
                                         using T = std::decay_t<decltype(gap_spec)>;
                                         if constexpr (std::is_same_v<T, S3URIFileChunkGapIndex>)
                                         {
                                             acc += 1; // single index gap
                                         }
                                         else if constexpr (std::is_same_v<T, S3URIFileChunkGapIndicesRange>)
                                         {
                                             acc += (gap_spec.to - gap_spec.from + 1); // range gap
                                         }
                                         return acc;
                                     },
                                     gap); });
    }

} // namespace gst::airtime