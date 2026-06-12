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

#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <istream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gstairtimes3bufferfillstatus.hpp"
#include "gstairtimes3urichunkspec.hpp"
#include "gstairtimes3uriobjectmetadata.hpp"

namespace gst::airtime
{

/// @brief Information about single index missing.
struct S3URIFileChunkGapIndex {
    std::uint64_t index;

    auto operator<=>(const S3URIFileChunkGapIndex&) const = default;
};

/// @brief Information about contiguous range of indices missing.
struct S3URIFileChunkGapIndicesRange {
    std::uint64_t from;
    std::uint64_t to; // inclusive

    auto operator<=>(const S3URIFileChunkGapIndicesRange&) const = default;
};

/// @brief A gap may concerns either a single index or a range of indices.
using S3URIFileChunkGapSpec = std::variant<S3URIFileChunkGapIndex, S3URIFileChunkGapIndicesRange>;

/// @brief A collection of gaps in the file chunks.
struct S3URIFileChunkGaps {
    /// @brief Indicates whether the gaps concern a full range of the file.
    /// If true, it means that all chunks are missing and the gaps vector is empty.
    /// If false, it means that there are gaps and the gaps vector contains the gaps.
    bool full_range{false};

    /// @brief A vector of gaps in the file chunks.
    /// If full_range is true, this vector is empty.
    /// If full_range is false, this vector contains the gaps.
    std::vector<S3URIFileChunkGapSpec> gaps;

    auto operator<=>(const S3URIFileChunkGaps&) const = default;
};

inline constexpr S3URIFileChunkGaps full_range_gaps{.full_range = true, .gaps = {}};
inline constexpr S3URIFileChunkGaps no_gaps{.full_range = false, .gaps = {}};

/// @brief Receives chunks from S3 and processes them.
class S3URIChunkProcessor
{
public:
    virtual ~S3URIChunkProcessor() = default;

    /// @brief Checks if the processor needs to know the S3 object metadata.
    /// @return true if the processor needs to know the S3 object metadata, false otherwise.
    virtual bool needsObjectMetadata() const = 0;

    /// @brief Sets the content length of the S3 object.
    /// @param content_length The content length of the S3 object, if known/if requested in needsContentLength method.
    virtual void setObjectMetadata(S3URIObjectMetadata metadata) = 0;

    /// @brief Gets the S3 object metadata.
    /// This method should return a valid S3 object metadata if needsObjectMetadata() returns false.
    /// @return The S3 object metadata.
    virtual S3URIObjectMetadata getObjectMetadata() const = 0;

    /// @brief Checks if the processor needs the object in chunks to be downloaded.
    /// It may happen that the chunks are already cached locally, in which case
    /// this method can return false to avoid unnecessary downloads.
    /// If cache is in consistent state but not all chunks are available, it may return information about
    /// missing chunks only.
    /// @return if first element of the pair is set to false, it means that no chunks are needed and in that case the
    /// second element of the pair is an empty vector. If first element of the pair is set to true it means that chunks
    /// are needed. In that case, if the second element is an empty vector it means that all chunks are needed,
    /// otherwise it contains information about which chunks are needed.
    virtual std::pair<bool, std::vector<S3URIFileChunkGapSpec>> needsChunks() const = 0;

    /// @brief Processes a downloaded chunk from S3.
    /// @param chunk_spec The specification of the chunk, including the byte range and priority.
    /// @param chunk_body The body of the downloaded chunk as an input stream.
    /// @note This method is called when a chunk is downloaded from S3.
    ///       It is expected to process the chunk and store it in a local cache or perform any other necessary
    ///       operations. The calling thread may be different from the one that called needs*/set*/ methods, so it is
    ///       important to ensure thread safety when accessing shared resources.
    virtual void processChunk(const S3URIChunkSpec& chunk_spec, std::istream& chunk_body) = 0;

    /// @brief Marks that all chunks have been processed.
    /// @return true if the cache is in a consistent state, false otherwise.
    virtual bool allChunksProcessed() = 0;

    virtual std::pair<S3BufferFillStatus, std::uint64_t> fill(std::uint8_t* data, std::uint64_t offset,
                                                              std::uint64_t size) = 0;
};

// ----------------------------------------------------------------------------------------------------------------- //

struct S3URICacheManagerEntry {
    std::filesystem::path cache_key_directory;
    S3URIObjectMetadata metadata;
    std::time_t last_access_time; ///< Last access time for LRU eviction policy.
};

/// @brief Base class for S3 URI cache eviction policies.
/// This class defines the interface for cache eviction policies used by S3URICacheManager.
/// It allows for different strategies to manage cache entries based on their metadata and access patterns.
class S3URICacheEvictionPolicy
{
public:
    virtual ~S3URICacheEvictionPolicy() = default;

    static constexpr std::uint64_t unlimitedCacheSize = std::numeric_limits<std::uint64_t>::max();

    /// @brief Returns the maximum size of the cache in bytes. Value std::numeric_limits<uint64_t>::max() means no
    /// limit.
    /// @param entries The list of cache entries to examine.
    /// @return The maximum size of the cache in bytes.
    virtual std::uint64_t getAvailableSize(const std::vector<S3URICacheManagerEntry>& entries) const = 0;

    /// @brief Adds a cache key to the eviction policy.
    /// @param metadata The S3 object metadata for the cache key.
    /// @param entries The list of cache entries to examine.
    /// @param current_cache_size The current size of the cache in bytes.
    /// @return The new size of the cache key in bytes.
    virtual std::uint64_t examineEntries(const S3URIObjectMetadata& metadata,
                                         std::vector<S3URICacheManagerEntry>& entries,
                                         std::uint64_t current_cache_size) = 0;
};

/// @brief LRU (Least Recently Used) cache eviction policy.
/// This policy evicts the least recently used entries when the cache size exceeds the maximum allowed size.
/// It is suitable for scenarios where the most recently accessed data is likely to be reused.
class LRUCacheEvictionPolicy : public S3URICacheEvictionPolicy
{
public:
    explicit LRUCacheEvictionPolicy(std::uint64_t max_cache_size);

    std::uint64_t getAvailableSize(const std::vector<S3URICacheManagerEntry>& entries) const override;

    std::uint64_t examineEntries(const S3URIObjectMetadata& metadata, std::vector<S3URICacheManagerEntry>& entries,
                                 std::uint64_t current_cache_size) override;

private:
    /// @brief Returns indices of unused entries sorted by last access time.
    /// @param entries The list of cache entries to sort.
    /// @return A vector of indices of entries sorted by last access time.
    std::vector<std::size_t>
    getLRUSortedAndUnusedEntriesIndices(const std::vector<S3URICacheManagerEntry>& entries) const;

    std::uint64_t max_cache_size_; ///< Maximum cache size in bytes.
};

/// @brief No rules cache eviction policy.
/// This policy does not evict any entries and keeps all entries in the cache.
/// It is suitable for scenarios where cache size is not a concern.
class NoRulesCacheEvictionPolicy : public S3URICacheEvictionPolicy
{
public:
    std::uint64_t getAvailableSize(const std::vector<S3URICacheManagerEntry>&) const override
    {
        return unlimitedCacheSize;
    }

    std::uint64_t examineEntries(const S3URIObjectMetadata&, std::vector<S3URICacheManagerEntry>&,
                                 std::uint64_t current_cache_size) override
    {
        return current_cache_size;
    }
};

/// @brief Manages the cache of S3 URI objects.
class S3URICacheManager
{
public:
    S3URICacheManager(std::filesystem::path cache_base_directory,
                      std::unique_ptr<S3URICacheEvictionPolicy> eviction_policy);

    /// @brief Checks if there is enough space in the cache for a new key.
    /// @param metadata The S3 object metadata for the cache key.
    /// @return True if there is enough space, false otherwise.
    bool isEnoughSpaceForKey(const S3URIObjectMetadata& metadata) const;

    /// @brief Adds a cache key to the eviction policy.
    /// @param metadata The S3 object metadata for the cache key.
    void keyRequested(const S3URIObjectMetadata& metadata);

    /// @brief Removes a cache key from the eviction policy.
    /// @param metadata The S3 object metadata for the cache key.
    void keyRemoved(const S3URIObjectMetadata& metadata);

    /// @brief Returns the current size of the cache.
    /// @return The current size of the cache in bytes.
    std::uint64_t currentCacheSize() const;

    /// @brief Returns the base directory for the cache.
    /// @return The base directory for the cache.
    const std::filesystem::path& cacheBaseDirectory() const noexcept
    {
        return cache_base_directory_;
    }

private:
    void loadCurrentCacheState();

    std::filesystem::path cache_base_directory_;
    std::unique_ptr<S3URICacheEvictionPolicy> eviction_policy_;
    std::vector<S3URICacheManagerEntry> entries_;
    std::uint64_t current_size_{0};
    mutable std::mutex entries_access_;
};

/// @brief Implementation of the S3URIChunkProcessor that caches S3 chunks fetched from S3 on a local filesystem.
class CachingS3URIChunkProcessor : public S3URIChunkProcessor
{
public:
    /// @param cache_manager The cache manager to use for caching S3 chunks.
    /// @param s3_bucket S3 bucket name.
    /// @param s3_key S3 object key.
    /// @param file_chunk_standard_size Standard size of file chunks.
    /// @param trust_cached_data Whether to trust the cached metadata (if exists) or not. This may avoid unnecessary
    /// metadata fetching.
    CachingS3URIChunkProcessor(std::shared_ptr<S3URICacheManager> cache_manager, std::string_view s3_bucket,
                               std::string_view s3_key, std::uint64_t file_chunk_standard_size, bool trust_cached_data);
    ~CachingS3URIChunkProcessor();

    bool needsObjectMetadata() const override;

    void setObjectMetadata(S3URIObjectMetadata metadata) override;

    S3URIObjectMetadata getObjectMetadata() const override;

    std::pair<bool, std::vector<S3URIFileChunkGapSpec>> needsChunks() const override;

    void processChunk(const S3URIChunkSpec& chunk_spec, std::istream& chunk_body) override;

    bool allChunksProcessed() override;

    std::pair<S3BufferFillStatus, std::uint64_t> fill(std::uint8_t* data, std::uint64_t offset,
                                                      std::uint64_t size) override;

    /// @brief Iterates over file_chunks_ and collect indices of the chunks that are missing.
    /// @return Information about missing chunks (if any).
    S3URIFileChunkGaps calculateChunkGaps() const;

private:
    void createDirectoryInUseFile();
    void removeDirectoryInUseFile();
    void loadChunksFromDirectory();
    void readCachedMetadataIfExists();
    void resetCacheDirectory(bool recreate_cache_content);
    S3URIFileChunkGaps calculateChunkGapsImpl() const;
    void fillGaps();

    struct CacheFileChunk {
        /// @brief The file path of the locally stored chunk.
        std::filesystem::path file_path;

        /// @brief The content length of the locally stored chunk.
        std::size_t content_length{0};

        /// @brief The index of the chunk in the cache.
        /// This is used to determine the gaps of chunks in the cache.
        std::uint64_t index{0};
    };

    std::shared_ptr<S3URICacheManager> cache_manager_;
    std::filesystem::path directory_path_;
    std::uint64_t file_chunk_standard_size_; // standard size of file chunks, used to estimate amount of file chunks
    bool trust_cached_data_;

    std::optional<S3URIObjectMetadata> cached_metadata_;
    std::optional<S3URIObjectMetadata> metadata_;
    mutable std::mutex state_access_;
    mutable std::vector<CacheFileChunk> file_chunks_;
    std::uint64_t file_chunks_content_length_{0};
    bool first_incoming_chunk_{true};
    mutable bool recreate_cache_content_{false};
};

// ----------------------------------------------------------------------------------------------------------------- //

/// @brief Extracts the index from the chunk file name.
/// The chunk file name is expected to be in the format "chunk_{index}.part", for instance:
/// "chunk_00000001.part" - index is 1.
/// "chunk_00000002.part" - index is 2.
/// @param chunk_file_path The path to the chunk file.
/// @return The index extracted from the chunk file name.
std::uint64_t extractIndexFromChunkFileName(const std::filesystem::path& chunk_file_path);

inline std::size_t calculateExpectedCacheFilesCount(std::uint64_t content_length,
                                                    std::uint64_t file_chunk_standard_size_bytes) noexcept
{
    return (content_length + file_chunk_standard_size_bytes - 1) / file_chunk_standard_size_bytes;
}

void serializeS3URIObjectMetadataToJSON(const S3URIObjectMetadata& metadata, std::ostream& json_output_stream);
void serializeS3URIObjectMetadataToJSON(const S3URIObjectMetadata& metadata,
                                        const std::filesystem::path& metadata_file);

S3URIObjectMetadata deserializeS3URIObjectMetadataFromJSON(std::istream& json_input_stream);
S3URIObjectMetadata deserializeS3URIObjectMetadataFromJSON(const std::filesystem::path& metadata_file);

void saveLastAccessTimeToFile(const std::filesystem::path& last_access_time_file, std::time_t last_access_time);
std::time_t loadLastAccessTimeFromFile(const std::filesystem::path& last_access_time_file);
std::time_t loadLastAccessTimeFromFileOrCurrent(const std::filesystem::path& last_access_time_file);

void saveFileChunkSizeToFile(const std::filesystem::path& file_chunk_size_file, std::uint64_t file_chunk_size);
void saveFileChunkSizeToFileIfNotExists(const std::filesystem::path& file_chunk_size_file,
                                        std::uint64_t file_chunk_size);
std::uint64_t loadFileChunkSizeFromFile(const std::filesystem::path& file_chunk_size_file);

/// @brief Calculates the total number of gaps in the provided chunk gaps.
/// @param gaps The chunk gaps to analyze.
/// @return The total number of gaps.
std::uint64_t calculateTotalGapsNumber(const std::vector<S3URIFileChunkGapSpec>& gaps);

} // namespace gst::airtime