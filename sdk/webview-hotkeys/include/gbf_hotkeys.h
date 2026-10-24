#pragma once
#include <stdint.h>
#ifdef _WIN32
#ifdef GBF_BUILD
#define GBF_API __declspec(dllexport)
#else
#define GBF_API __declspec(dllimport)
#endif
#else
#define GBF_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* ABI 1, Windows x64. All calls for an instance are serialized by its owner.
 * HWND arguments: foreground top-level window and game viewport child HWND.
 * No keyboard registration or global hook is installed by this library.
 * UTF-8 reply lives until the next command/destroy; copy before asynchronous use.
 */
GBF_API uint32_t gbf_abi_version(void);
GBF_API void* gbf_create(uintptr_t owner, uintptr_t viewport);
GBF_API const char* gbf_command(void* instance, const char* json_utf8);
GBF_API const char* gbf_probe_script(void);
GBF_API void gbf_destroy(void* instance);
#ifdef __cplusplus
}
#endif
