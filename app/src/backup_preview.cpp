#include "backup_preview.h"
#include "backup_data.h"
#include "core.h"

namespace gbrowser {
std::wstring DescribeBackupSummary(const BackupImportSummary& input) {
  std::wstring summary = L"资料名称：" + input.name +
      L"\r\n备份声明的登录态选项：" + (input.include_login ? L"包含" : L"不包含") +
      L"\r\n\r\n备份声明的数据项：\r\n";
  for (const auto& record : input.records) {
    const auto label = record.source.kind == BackupSourceKind::BrowserSettings ? L"浏览器设置" :
        record.source.kind == BackupSourceKind::GbfSession ? L"GBF 登录态" :
        record.source.kind == BackupSourceKind::Extension ? L"扩展数据" : L"脚本数据";
    summary += std::wstring(label) + L" · " + Utf8ToWide(record.source.id) + L" · 数据版本 " +
        std::to_wstring(record.source.schema) + L" · " + std::to_wstring(record.bytes) + L" 字节\r\n";
  }
  return summary;
}
std::wstring InspectBackupFile(const std::filesystem::path& source, const SensitiveBytes& password) {
  const auto plaintext = LoadBackupFile(source, password);
  const auto document = DecodeBackupDocument(plaintext);
  BackupImportSummary summary{document.name, document.include_login, {}};
  summary.records.reserve(document.records.size());
  for (const auto& record : document.records) summary.records.push_back({record.source, record.data.bytes().size()});
  return DescribeBackupSummary(summary) + L"\r\n文件密码认证通过。这里只显示备份声明，不展示具体数据。\r\n"
      L"恢复兼容性尚未核验；查看不会导入、创建或切换资料。";
  // Document/plaintext SensitiveBytes are erased before the UI receives it.
}
}  // namespace gbrowser
