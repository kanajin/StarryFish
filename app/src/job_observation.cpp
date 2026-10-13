#include "job_observation.h"

namespace gbrowser {
namespace {
template <class Information>
JobQueryStatus Query(HANDLE job, JOBOBJECTINFOCLASS kind, Information& information,
                     DWORD& error, DWORD& bytes) noexcept {
  if (!QueryInformationJobObject(job, kind, &information, sizeof(information), &bytes)) {
    error = GetLastError();
    return JobQueryStatus::ApiFailure;
  }
  if (bytes != sizeof(information)) return JobQueryStatus::LengthMismatch;
  return JobQueryStatus::Observed;
}
}  // namespace

JobLimitsObservation ObserveJobLimits(HANDLE job) noexcept {
  JobLimitsObservation result;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION extended{};
  result.extended_status = Query(job, JobObjectExtendedLimitInformation, extended,
                                result.extended_error, result.extended_bytes);
  if (result.extended_status == JobQueryStatus::Observed) {
    const auto& basic = extended.BasicLimitInformation;
    result.extended = JobLimits{basic.LimitFlags, basic.ActiveProcessLimit, basic.PriorityClass,
        basic.Affinity, basic.MinimumWorkingSetSize, basic.MaximumWorkingSetSize,
        extended.ProcessMemoryLimit, extended.JobMemoryLimit,
        basic.PerProcessUserTimeLimit.QuadPart, basic.PerJobUserTimeLimit.QuadPart};
  }
  JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
  result.ui_status = Query(job, JobObjectBasicUIRestrictions, ui, result.ui_error, result.ui_bytes);
  if (result.ui_status == JobQueryStatus::Observed) result.ui_restrictions = ui.UIRestrictionsClass;
  return result;
}
CurrentJobObservation ObserveCurrentJob() noexcept {
  CurrentJobObservation result;
  BOOL in_job = FALSE;
  if (!IsProcessInJob(GetCurrentProcess(), nullptr, &in_job)) {
    result.membership_error = GetLastError();
    return result;
  }
  result.membership_known = true;
  result.in_job = in_job != FALSE;
  if (result.in_job) result.limits = ObserveJobLimits(nullptr);
  return result;
}
}  // namespace gbrowser
