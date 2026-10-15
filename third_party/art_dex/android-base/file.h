#ifndef KUDROID_ANDROID_BASE_FILE_H_
#define KUDROID_ANDROID_BASE_FILE_H_

#include <sys/stat.h>

#include <string>

namespace android {
namespace base {

bool ReadFileToString(const std::string& path, std::string* content,
                      bool follow_symlinks = false);
bool WriteStringToFile(const std::string& content, const std::string& path,
                       bool follow_symlinks = false);
bool ReadFdToString(int fd, std::string* content);
bool ReadFully(int fd, void* data, size_t byte_count);
bool WriteFully(int fd, const void* data, size_t byte_count);

}  // namespace base
}  // namespace android

#endif  // KUDROID_ANDROID_BASE_FILE_H_
