// Copyright 2021 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "open_spiel/utils/file.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdlib>

#ifdef _WIN32
// https://stackoverflow.com/a/42906151
#include <direct.h>
#include <stdio.h>
#include <windows.h>
#define mkdir(dir, mode) _mkdir(dir)
#define unlink(file) _unlink(file)
#define rmdir(dir) _rmdir(dir)
#else
#include <unistd.h>
#endif

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "open_spiel/spiel_utils.h"

namespace open_spiel::file {

class File::FileImpl : public std::FILE {};

namespace internal {

namespace {

// Returns the index at which recursive directory creation should begin for the
// supplied Windows path. This ignores drive letter prefixes (e.g. `C:`) and UNC
// share prefixes (e.g. `\\server\\share`). For non-Windows platforms the
// prefix length is always 0.
size_t WindowsRootPrefixLengthImpl(const std::string& path) {
#ifdef _WIN32
  if (path.size() > 1 && path[1] == ':') {
    // Skip the drive letter and optional separator (e.g. "C:" or "C:\").
    size_t prefix = 2;
    if (path.size() > 2 && (path[2] == '\\' || path[2] == '/')) {
      ++prefix;
    }
    return prefix;
  }

  if (path.size() > 1 && path[0] == '\\' && path[1] == '\\') {
    // Skip the server component.
    size_t pos = path.find_first_of("\\/", 2);
    if (pos == std::string::npos) {
      return path.size();
    }
    // Skip the share component if present.
    pos = path.find_first_of("\\/", pos + 1);
    if (pos == std::string::npos) {
      return path.size();
    }
    return pos;
  }
#endif
  (void)path;
  return 0;
}

}  // namespace

size_t WindowsRootPrefixLength(const std::string& path) {
  return WindowsRootPrefixLengthImpl(path);
}

}  // namespace internal

File::File(const std::string& filename, const std::string& mode) {
  fd_.reset(static_cast<FileImpl*>(std::fopen(filename.c_str(), mode.c_str())));
  SPIEL_CHECK_TRUE(fd_);
}

File::~File() {
  if (fd_) {
    Flush();
    Close();
  }
}

File::File(File&& other) = default;
File& File::operator=(File&& other) = default;

bool File::Close() { return !std::fclose(fd_.release()); }
bool File::Flush() { return !std::fflush(fd_.get()); }
std::int64_t File::Tell() { return std::ftell(fd_.get()); }
bool File::Seek(std::int64_t offset) {
  return !std::fseek(fd_.get(), offset, SEEK_SET);
}

std::string File::Read(std::int64_t count) {
  std::string out(count, '\0');
  int read = std::fread(out.data(), sizeof(char), count, fd_.get());
  out.resize(read);
  return out;
}

std::string File::ReadContents() {
  Seek(0);
  return Read(Length());
}

bool File::Write(absl::string_view str) {
  return std::fwrite(str.data(), sizeof(char), str.size(), fd_.get()) ==
         str.size();
}

std::int64_t File::Length() {
  std::int64_t current = std::ftell(fd_.get());
  std::fseek(fd_.get(), 0, SEEK_END);
  std::int64_t length = std::ftell(fd_.get());
  std::fseek(fd_.get(), current, SEEK_SET);
  return length;
}

std::string ReadContentsFromFile(const std::string& filename,
                                 const std::string& mode) {
  File f(filename, mode);
  return f.ReadContents();
}

void WriteContentsToFile(const std::string& filename, const std::string& mode,
                         const std::string& contents) {
  File f(filename, mode);
  f.Write(contents);
}

bool Exists(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}

std::string RealPath(const std::string& path) {
#ifdef _WIN32
  char real_path[MAX_PATH];
  if (_fullpath(real_path, path.c_str(), MAX_PATH) == nullptr) {
#else
  char real_path[PATH_MAX];
  if (realpath(path.c_str(), real_path) == nullptr) {
    // If there was an error return an empty path
#endif
    return "";
  }

  return std::string(real_path);
}

bool IsDirectory(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0 && info.st_mode & S_IFDIR;
}

bool Mkdir(const std::string& path, int mode) {
  return mkdir(path.c_str(), mode) == 0;
}

bool Mkdirs(const std::string& path, int mode) {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::path fs_path(path);
  if (std::filesystem::create_directories(fs_path, ec)) {
#ifndef _WIN32
    // Best effort to apply the requested permissions on POSIX platforms.
    chmod(path.c_str(), mode);
#endif
    return true;
  }

  if (!ec && std::filesystem::exists(fs_path)) {
    return std::filesystem::is_directory(fs_path);
  }

  // Fall back to the manual implementation for platforms or situations where
  // std::filesystem fails (e.g. due to missing support or specific error
  // codes). This retains the previous behaviour and allows us to honour the
  // requested mode.
  struct stat info;
  size_t pos = internal::WindowsRootPrefixLength(path);
  while (pos != std::string::npos) {
    pos = path.find_first_of("\\/", pos + 1);
    std::string sub_path = path.substr(0, pos);
    if (sub_path.empty()) {
      continue;
    }
    if (stat(sub_path.c_str(), &info) == 0) {
      if (info.st_mode & S_IFDIR) {
        continue;  // directory already exists
      } else {
        return false;  // is a file?
      }
    }
    if (!Mkdir(sub_path, mode)) {
      return false;  // permission error?
    }
  }
  return true;
}

bool Remove(const std::string& path) {
  if (IsDirectory(path)) {
    return rmdir(path.c_str()) == 0;
  } else {
    return unlink(path.c_str()) == 0;
  }
}

std::string GetEnv(const std::string& key, const std::string& default_value) {
  char* val = std::getenv(key.c_str());
  return ((val != nullptr) ? std::string(val) : default_value);
}

std::string GetTmpDir() { return GetEnv("TMPDIR", "/tmp"); }

}  // namespace open_spiel::file
