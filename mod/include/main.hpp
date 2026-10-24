#pragma once

#include "scotland2/shared/modloader.h"

#include "paper/shared/logger.hpp"

#define MOD_EXPORT __attribute__((visibility("default")))
#define MOD_EXTERN_FUNC extern "C" MOD_EXPORT

constexpr auto Log = Paper::ConstLoggerContext("octantpath");
