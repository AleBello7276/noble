#pragma once
#include <atomic>
#include <stdint.h>

#include "KObject.h"
#include "core/endian.h"
#include "cpu/PpcContext.h"

struct X_KEVENT {
    X_DISPATCH_HEADER header;
};
