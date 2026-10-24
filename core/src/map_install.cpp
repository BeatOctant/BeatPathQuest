#include "octantpath/core/map_install.hpp"

#include "octantpath/core/beatsaver.hpp"
#include "octantpath/core/sha1.hpp"

#include <miniz/miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace octantpath::core {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

InstallResult fail(InstallFailure failure, std::string detail = {}) { return {.failure = failure, .detail = std::move(detail)}; }

std::optional<std::string> readFile(fs::path const &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}

std::string slashes(std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  return name;
}

std::string basename(std::string const &name) {
  auto slash = name.find_last_of('/');
  return slash == std::string::npos ? name : name.substr(slash + 1);
}

std::vector<std::string> referencedFiles(json const &info) {
  std::vector<std::string> names;
  auto version = info.value("version", std::string());
  if (!version.empty() && version[0] == '4') {
    names.push_back(info.at("audio").at("audioDataFilename").get<std::string>());
    for (auto const &diff : info.at("difficultyBeatmaps")) {
      names.push_back(diff.at("beatmapDataFilename").get<std::string>());
      names.push_back(diff.at("lightshowDataFilename").get<std::string>());
    }
  } else {
    for (auto const &set : info.at("_difficultyBeatmapSets")) {
      for (auto const &diff : set.at("_difficultyBeatmaps")) names.push_back(diff.at("_beatmapFilename").get<std::string>());
    }
  }
  if (std::any_of(names.begin(), names.end(), [](auto const &n) { return n.empty(); })) throw std::runtime_error("an empty file name");
  return names;
}

std::string randomName() {
  std::random_device device;
  char buffer[17];
  std::snprintf(buffer, sizeof buffer, "%08x%08x", device(), device());
  return buffer;
}

void removeQuietly(fs::path const &path) {
  std::error_code error;
  fs::remove_all(path, error);
}

// 1 つの中身を書き出す。展開した大きさの合計が上限を超えたら止める
struct Writer {
  std::FILE *file;
  uint64_t *total;
  uint64_t limit;
  bool overflow = false;
};

size_t writeChunk(void *opaque, mz_uint64, void const *data, size_t size) {
  auto *writer = static_cast<Writer *>(opaque);
  *writer->total += size;
  if (*writer->total > writer->limit) {
    writer->overflow = true;
    return 0;
  }
  return std::fwrite(data, 1, size, writer->file);
}

} // namespace

bool safeEntryName(std::string const &name) {
  if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
  std::string part;
  std::stringstream parts(slashes(name));
  while (std::getline(parts, part, '/')) {
    if (part.empty() || part == "." || part == "..") return false;
  }
  return slashes(name).back() != '/';
}

bool safeFolderName(std::string const &name) {
  static std::string const unsafe = "/\\:*?\"<>|";
  if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') return false;
  if (name.find_first_not_of(" \t") == std::string::npos) return false;
  return std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || unsafe.find(static_cast<char>(c)) != std::string::npos; });
}

std::string folderName(std::string const &key, std::string const &songName, std::string const &mapper) {
  static std::string const unsafe = "/\\:*?\"<>|";
  auto label = key + " (" + songName + " - " + mapper + ")";
  std::string cleaned;
  size_t codepoints = 0;
  for (size_t i = 0; i < label.size() && codepoints < 80;) {
    auto c = static_cast<unsigned char>(label[i]);
    size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (length == 1) cleaned += (c < 0x20 || c == 0x7f || unsafe.find(static_cast<char>(c)) != std::string::npos) ? '_' : static_cast<char>(c);
    else cleaned += label.substr(i, length);
    i += length;
    ++codepoints;
  }
  while (!cleaned.empty() && (cleaned.back() == '.' || cleaned.back() == ' ')) cleaned.pop_back();
  return cleaned;
}

MapHashResult computeMapHash(fs::path const &folder) {
  MapHashResult result;
  fs::path infoPath;
  for (auto name : {"Info.dat", "info.dat"}) {
    if (fs::is_regular_file(folder / name)) {
      infoPath = folder / name;
      break;
    }
  }
  if (infoPath.empty()) return {.failure = InstallFailure::NoInfo};
  auto infoText = readFile(infoPath);
  if (!infoText) return {.failure = InstallFailure::InfoUnreadable};
  // PC 版は Info.dat を文字列として読み、UTF-8 に戻して足す（先頭の BOM は落ちる）
  if (infoText->rfind("\xEF\xBB\xBF", 0) == 0) infoText->erase(0, 3);
  std::vector<std::string> names;
  try {
    names = referencedFiles(json::parse(*infoText));
  } catch (json::parse_error const &e) {
    return {.failure = InstallFailure::InfoUnreadable, .detail = e.what()};
  } catch (std::exception const &e) {
    return {.failure = InstallFailure::InfoMalformed, .detail = e.what()};
  }
  Sha1 sha1;
  sha1.update(*infoText);
  for (auto const &name : names) {
    if (!safeEntryName(name)) return {.failure = InstallFailure::InfoPointsOutside, .detail = name};
    auto bytes = readFile(folder / slashes(name));
    if (!bytes || !fs::is_regular_file(folder / slashes(name))) return {.failure = InstallFailure::InfoFileMissing, .detail = name};
    sha1.update(*bytes);
  }
  result.hash = sha1.hex();
  result.files.push_back(infoPath.filename().string());
  result.files.insert(result.files.end(), names.begin(), names.end());
  return result;
}

InstallResult extractMapZip(std::string const &zip, fs::path const &destination, Settings const &settings) {
  mz_zip_archive archive{};
  if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) return fail(InstallFailure::ZipUnreadable, mz_zip_get_error_string(mz_zip_get_last_error(&archive)));
  struct Closer {
    mz_zip_archive *archive;
    ~Closer() { mz_zip_reader_end(archive); }
  } closer{&archive};

  auto count = mz_zip_reader_get_num_files(&archive);
  if (count > static_cast<mz_uint>(settings.zipMaxEntries)) return fail(InstallFailure::TooManyEntries);
  std::vector<std::string> names;
  std::vector<std::string> infos;
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, i, &stat)) return fail(InstallFailure::ZipUnreadable);
    auto name = slashes(stat.m_filename);
    auto trimmed = name;
    while (!trimmed.empty() && trimmed.back() == '/') trimmed.pop_back();
    if (!safeEntryName(trimmed)) return fail(InstallFailure::BadEntryName, stat.m_filename);
    if (lower(basename(trimmed)) == "info.dat" && !stat.m_is_directory) infos.push_back(trimmed);
    names.push_back(name);
  }
  // マップの根は Info.dat のある場所。1 つに決まらなければ入れない
  if (infos.size() != 1) return fail(infos.empty() ? InstallFailure::NoInfo : InstallFailure::ManyRoots);
  auto slash = infos[0].find_last_of('/');
  auto prefix = slash == std::string::npos ? std::string() : infos[0].substr(0, slash + 1);

  std::error_code error;
  fs::create_directories(destination, error);
  if (error) return fail(InstallFailure::Io, error.message());
  uint64_t total = 0;
  auto limit = static_cast<uint64_t>(settings.zipMaxExtractedMegabytes) * 1024 * 1024;
  for (mz_uint i = 0; i < count; ++i) {
    auto const &name = names[i];
    if (name.compare(0, prefix.size(), prefix) != 0) continue;
    auto relative = name.substr(prefix.size());
    if (relative.empty() || relative.back() == '/') continue;
    auto path = destination / relative;
    fs::create_directories(path.parent_path(), error);
    if (error) return fail(InstallFailure::Io, error.message());
    if (fs::exists(path)) return fail(InstallFailure::BadEntryName, name);
    auto *file = std::fopen(path.c_str(), "wb");
    if (!file) return fail(InstallFailure::Io, path.string());
    Writer writer{file, &total, limit};
    auto extracted = mz_zip_reader_extract_to_callback(&archive, i, writeChunk, &writer, 0);
    auto closed = std::fclose(file) == 0;
    if (writer.overflow) return fail(InstallFailure::ExtractedTooLarge);
    if (!extracted) return fail(InstallFailure::ZipUnreadable, name);
    if (!closed) return fail(InstallFailure::Io, path.string());
  }
  return {};
}

InstallResult installMap(std::string const &expectedHash, std::string const &zip, std::string const &name, fs::path const &levelsDirectory, fs::path const &workDirectory,
                         Settings const &settings) {
  auto hash = normalizeHash(expectedHash);
  if (!hash) return fail(InstallFailure::BadHash);
  if (!safeFolderName(name)) return fail(InstallFailure::BadFolderName, name);
  auto target = levelsDirectory / name;
  if (fs::exists(target)) return fail(InstallFailure::NameTaken, name);
  if (zip.size() > static_cast<uint64_t>(settings.zipMaxDownloadMegabytes) * 1024 * 1024) return fail(InstallFailure::ZipTooLarge);

  auto job = workDirectory / randomName();
  std::error_code error;
  fs::create_directories(job, error);
  if (error) return fail(InstallFailure::Io, error.message());
  struct Cleanup {
    fs::path path;
    ~Cleanup() { removeQuietly(path); }
  } cleanup{job};

  auto extracted = job / "map";
  if (auto result = extractMapZip(zip, extracted, settings); !result.ok()) return result;
  auto computed = computeMapHash(extracted);
  if (computed.hash.empty()) return fail(computed.failure, computed.detail);
  if (computed.hash != *hash) return fail(InstallFailure::HashMismatch, computed.hash);

  fs::create_directories(levelsDirectory, error);
  if (error) return fail(InstallFailure::Io, error.message());
  if (fs::exists(target)) return fail(InstallFailure::NameTaken, name);
  fs::rename(extracted, target, error);
  if (error) return fail(InstallFailure::Io, error.message());
  return {.path = target, .hash = computed.hash};
}

} // namespace octantpath::core
