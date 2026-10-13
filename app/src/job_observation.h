#pragma once
#include <windows.h>
#include <optional>

namespace gbrowser {
// Retained to investigate the incomplete Windows P00 sandbox gate. Helper
// tests and callback samples do not establish runtime sandbox compliance.
// Read-only fixed-size observations, not a sandbox verdict. NULL queries the
// calling process's immediate job only; it does not enumerate nested ancestors.
enum class JobQueryStatus { NotAttempted, Observed, ApiFailure, LengthMismatch };
struct JobLimits {
  DWORD flags = 0, active_process_limit = 0, priority_class = 0;
  ULONG_PTR affinity = 0;
  SIZE_T minimum_working_set = 0, maximum_working_set = 0;
  SIZE_T process_memory = 0, job_memory = 0;
  LONGLONG process_time = 0, job_time = 0;
};
struct JobLimitsObservation {
  JobQueryStatus extended_status = JobQueryStatus::NotAttempted;
  DWORD extended_error = 0, extended_bytes = 0;
  std::optional<JobLimits> extended;
  JobQueryStatus ui_status = JobQueryStatus::NotAttempted;
  DWORD ui_error = 0, ui_bytes = 0;
  std::optional<DWORD> ui_restrictions;
};
// Borrows a trusted existing handle, never opens, changes or closes a job.
// Values are raw; individual limits apply only when their flags are present.
JobLimitsObservation ObserveJobLimits(HANDLE job) noexcept;
struct CurrentJobObservation {
  bool membership_known = false, in_job = false;
  DWORD membership_error = 0;
  JobLimitsObservation limits;
};
// Membership failure/no job leaves both limit queries NotAttempted. Even a
// successful result is a point-in-time sample of the immediate job, not proof
// of all effective restrictions, job provenance or the other CEF processes.
CurrentJobObservation ObserveCurrentJob() noexcept;
}  // namespace gbrowser
