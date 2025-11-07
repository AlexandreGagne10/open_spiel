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

#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif


#include "open_spiel/abseil-cpp/absl/random/distributions.h"
#include "open_spiel/spiel_utils.h"

constexpr int kMaxNumAttempts = 100;
constexpr int kMaxRandomVal = 1000000000;

namespace open_spiel::file {
namespace {

void TestFile() {
  std::mt19937 rng(std::time(nullptr));
  std::string val = std::to_string(absl::Uniform<int>(rng, 0, kMaxRandomVal));
  std::string tmp_dir = file::GetTmpDir();
  std::string dir = tmp_dir + "/open_spiel-test-" + val;
  std::string filename = dir + "/test-file.txt";

  SPIEL_CHECK_TRUE(Exists(tmp_dir));
  SPIEL_CHECK_TRUE(IsDirectory(tmp_dir));

  // Create a directory with a random name. Sometimes this directory happens
  // to already exist. So we keep trying until we succeed or run
  // out of directories.
  int attempts = 0;
  while (Exists(dir) && attempts < kMaxNumAttempts) {
    val = std::to_string(absl::Uniform<int>(rng, 0, kMaxRandomVal));
    dir = tmp_dir + "/open_spiel-test-" + val;
  }

  SPIEL_CHECK_FALSE(Exists(dir));
  SPIEL_CHECK_TRUE(Mkdir(dir));
  SPIEL_CHECK_FALSE(Mkdir(dir));  // already exists
  SPIEL_CHECK_TRUE(Exists(dir));
  SPIEL_CHECK_TRUE(IsDirectory(dir));

  std::string prefix = "hello world ";
  std::string expected = prefix + val + "\n";
  {
    File f(filename, "w");
    SPIEL_CHECK_EQ(f.Tell(), 0);
    SPIEL_CHECK_TRUE(f.Write(expected));
    SPIEL_CHECK_TRUE(f.Flush());
    SPIEL_CHECK_EQ(f.Tell(), expected.size());
    SPIEL_CHECK_EQ(f.Length(), expected.size());
  }

  SPIEL_CHECK_TRUE(Exists(filename));
  SPIEL_CHECK_FALSE(IsDirectory(filename));
  // Ensure that realpath returns a string.
  SPIEL_CHECK_FALSE(RealPath(filename).empty());

  {
    File f(filename, "r");
    SPIEL_CHECK_EQ(f.Tell(), 0);
    SPIEL_CHECK_EQ(f.Length(), expected.size());
    std::string found = f.ReadContents();
    SPIEL_CHECK_EQ(found, expected);
    SPIEL_CHECK_EQ(f.Tell(), expected.size());
    f.Seek(0);
    SPIEL_CHECK_EQ(f.Read(6), "hello ");
    SPIEL_CHECK_EQ(f.Read(6), "world ");
  }

  { // Test the move constructor/assignment.
    File f(filename, "r");
    File f2 = std::move(f);
    File f3(std::move(f2));
  }

  SPIEL_CHECK_TRUE(Remove(filename));
  SPIEL_CHECK_FALSE(Remove(filename));  // already gone
  SPIEL_CHECK_FALSE(Exists(filename));

  std::string deep_dir = dir + "/1/2/3";
  SPIEL_CHECK_FALSE(IsDirectory(dir + "/1"));
  SPIEL_CHECK_TRUE(Mkdirs(dir + "/1/2/3"));
  SPIEL_CHECK_TRUE(IsDirectory(dir + "/1/2/3"));
  SPIEL_CHECK_TRUE(Remove(dir + "/1/2/3"));
  SPIEL_CHECK_TRUE(Remove(dir + "/1/2"));
  SPIEL_CHECK_TRUE(Remove(dir + "/1"));

  SPIEL_CHECK_TRUE(Remove(dir));
  SPIEL_CHECK_FALSE(Exists(dir));
}

#ifdef _WIN32
std::string NormalizeDirPathForTest(std::string path) {
  while (path.size() > 1 &&
         (path.back() == '\\' || path.back() == '/')) {
    if (path.size() == 3 && path[1] == ':' &&
        ((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z'))) {
      break;
    }
    path.pop_back();
  }
  return path;
}

struct EnvRestorer {
  EnvRestorer(std::string tmp_value, std::string temp_value,
              std::string localappdata_value)
      : tmp(std::move(tmp_value)),
        temp(std::move(temp_value)),
        localappdata(std::move(localappdata_value)) {}
  ~EnvRestorer() {
    _putenv_s("TMP", tmp.c_str());
    _putenv_s("TEMP", temp.c_str());
    _putenv_s("LOCALAPPDATA", localappdata.c_str());
  }
  std::string tmp;
  std::string temp;
  std::string localappdata;
};

void TestWindowsTmpDirResolution() {
  std::mt19937 rng(std::time(nullptr));
  std::string unique =
      std::to_string(absl::Uniform<int>(rng, 0, kMaxRandomVal));
  std::string base_tmp = file::GetTmpDir();

  std::string tmp_dir_1 = base_tmp + "/open_spiel-test-win-tmp-1-" + unique;
  std::string tmp_dir_2 = base_tmp + "/open_spiel-test-win-tmp-2-" + unique;
  std::string tmp_dir_3 = base_tmp + "/open_spiel-test-win-tmp-3-" + unique;

  if (Exists(tmp_dir_1)) {
    SPIEL_CHECK_TRUE(Remove(tmp_dir_1));
  }
  SPIEL_CHECK_TRUE(Mkdir(tmp_dir_1));
  if (Exists(tmp_dir_2)) {
    SPIEL_CHECK_TRUE(Remove(tmp_dir_2));
  }
  SPIEL_CHECK_TRUE(Mkdir(tmp_dir_2));
  if (Exists(tmp_dir_3)) {
    SPIEL_CHECK_TRUE(Remove(tmp_dir_3));
  }
  SPIEL_CHECK_TRUE(Mkdir(tmp_dir_3));

  EnvRestorer restore_env(GetEnv("TMP", ""), GetEnv("TEMP", ""),
                          GetEnv("LOCALAPPDATA", ""));

  _putenv_s("TMP", tmp_dir_1.c_str());
  _putenv_s("TEMP", tmp_dir_2.c_str());
  _putenv_s("LOCALAPPDATA", tmp_dir_3.c_str());
  SPIEL_CHECK_EQ(file::GetTmpDir(), tmp_dir_1);

  _putenv_s("TMP", "");
  SPIEL_CHECK_EQ(file::GetTmpDir(), tmp_dir_2);

  _putenv_s("TEMP", "");
  SPIEL_CHECK_EQ(file::GetTmpDir(), tmp_dir_3);

  _putenv_s("LOCALAPPDATA", "");
  char buffer[MAX_PATH];
  DWORD length = GetTempPathA(MAX_PATH, buffer);
  SPIEL_CHECK_GT(length, 0);
  std::string expected(buffer, length);
  expected = NormalizeDirPathForTest(expected);
  SPIEL_CHECK_EQ(file::GetTmpDir(), expected);

  SPIEL_CHECK_TRUE(Remove(tmp_dir_1));
  SPIEL_CHECK_TRUE(Remove(tmp_dir_2));
  SPIEL_CHECK_TRUE(Remove(tmp_dir_3));
}
#else
struct PosixTmpdirRestorer {
  PosixTmpdirRestorer() {
    const char* value = std::getenv("TMPDIR");
    if (value != nullptr) {
      had_value = true;
      original = value;
    }
  }
  ~PosixTmpdirRestorer() {
    if (had_value) {
      setenv("TMPDIR", original.c_str(), 1);
    } else {
      unsetenv("TMPDIR");
    }
  }
  bool had_value = false;
  std::string original;
};

void TestPosixTmpDirResolution() {
  PosixTmpdirRestorer restore_env;
  setenv("TMPDIR", "/path/that/should/not/exist", 1);

  std::string fallback = file::GetTmpDir();
  if (IsDirectory("/tmp")) {
    SPIEL_CHECK_EQ(fallback, "/tmp");
  } else if (IsDirectory("/var/tmp")) {
    SPIEL_CHECK_EQ(fallback, "/var/tmp");
  } else {
    SPIEL_CHECK_EQ(fallback, ".");
  }

  std::mt19937 rng(std::time(nullptr));
  std::string unique =
      std::to_string(absl::Uniform<int>(rng, 0, kMaxRandomVal));

  std::string base_dir = fallback;
  if (!IsDirectory(base_dir)) {
    base_dir = file::GetTmpDir();
  }
  if (!IsDirectory(base_dir)) {
    base_dir = ".";
  }

  std::string custom = base_dir + "/open_spiel-test-posix-tmp-" + unique;
  if (Exists(custom)) {
    SPIEL_CHECK_TRUE(Remove(custom));
  }
  SPIEL_CHECK_TRUE(Mkdir(custom));
  setenv("TMPDIR", (custom + "/").c_str(), 1);
  SPIEL_CHECK_EQ(file::GetTmpDir(), custom);
  SPIEL_CHECK_TRUE(Remove(custom));
}
#endif

}  // namespace
}  // namespace open_spiel::file

int main(int argc, char** argv) {
  open_spiel::file::TestFile();
#ifdef _WIN32
  open_spiel::file::TestWindowsTmpDirResolution();
#else
  open_spiel::file::TestPosixTmpDirResolution();
#endif
}
