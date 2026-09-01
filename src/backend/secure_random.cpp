#include "secure_random.h"

#include <cerrno>
#include <climits>
#include <cstddef>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <bcrypt.h>
#elif defined(__APPLE__)
#  include <Security/SecRandom.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__linux__)
#    include <sys/random.h>
#  endif
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
static quantapdf_status quantapdf_secure_random_urandom(
    unsigned char *data,
    size_t size)
{
    int descriptor;
    do {
        descriptor = open("/dev/urandom", O_RDONLY
#  if defined(O_CLOEXEC)
            | O_CLOEXEC
#  endif
        );
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return QUANTAPDF_ERROR_IO;

    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(descriptor, data + offset, size - offset);
        if (count > 0) {
            offset += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        (void)close(descriptor);
        return QUANTAPDF_ERROR_IO;
    }
    if (close(descriptor) != 0)
        return QUANTAPDF_ERROR_IO;
    return QUANTAPDF_OK;
}
#endif

extern "C" quantapdf_status quantapdf_secure_random(
    unsigned char *data,
    size_t size)
{
    if (data == nullptr && size != 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (size == 0)
        return QUANTAPDF_OK;

#if defined(_WIN32)
    size_t offset = 0;
    while (offset < size) {
        size_t remaining = size - offset;
        ULONG count = remaining > static_cast<size_t>(ULONG_MAX)
            ? ULONG_MAX
            : static_cast<ULONG>(remaining);
        NTSTATUS status = BCryptGenRandom(
            nullptr, data + offset, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0)
            return QUANTAPDF_ERROR_BACKEND;
        offset += count;
    }
    return QUANTAPDF_OK;
#elif defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault, size, data) == errSecSuccess
        ? QUANTAPDF_OK
        : QUANTAPDF_ERROR_BACKEND;
#else
#  if defined(__linux__)
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = getrandom(data + offset, size - offset, 0);
        if (count > 0) {
            offset += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == ENOSYS || errno == EPERM))
            break;
        return QUANTAPDF_ERROR_IO;
    }
    if (offset == size)
        return QUANTAPDF_OK;
#  endif
    return quantapdf_secure_random_urandom(data, size);
#endif
}
