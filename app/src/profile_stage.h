#pragma once
#include "backup.h"
#include <string>
#include <utility>

namespace gbrowser {
class ProfileStore;
// Retained for FR04 default-profile creation and FR05 verified new-profile
// publication; it already has production and synthetic-test consumers.
// Synchronous, local staging capability for trusted restoration code. It is
// not a file-copy adapter for Chromium storage or an authenticated importer.
// No raw root path is exposed. Any failed operation poisons publication.
// Observed payloads/metadata must be ordinary single-link files; checks are
// point-in-time, not a filesystem lease or isolation from same-user mutation.
class NewProfileStage {
 public:
  void Write(const std::string& relative, const SensitiveBytes& data);
  SensitiveBytes Read(const std::string& relative) const;
  size_t file_count() const { return entries_.size(); }
  NewProfileStage(const NewProfileStage&) = delete;
  NewProfileStage& operator=(const NewProfileStage&) = delete;
  NewProfileStage(NewProfileStage&&) = delete;
  NewProfileStage& operator=(NewProfileStage&&) = delete;
 private:
  explicit NewProfileStage(std::filesystem::path root) : root_(std::move(root)) {}
  struct Entry { std::string path; SensitiveBytes expected; };
  void Seal();
  void CheckIntegrity() const;
  std::filesystem::path root_;
  std::vector<Entry> entries_;
  size_t total_ = 0;
  bool sealed_ = false;
  mutable bool failed_ = false;
  friend class ProfileStore;
};
}  // namespace gbrowser
