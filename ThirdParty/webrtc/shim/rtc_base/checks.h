// Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see ../../LICENSE.
// Noise Suppression local shim (NOT upstream WebRTC). Replaces rtc_base/checks.h for the vendored
// noise suppressor and three-band filter bank. Debug: assert()-style check. Release (NDEBUG): no-op,
// the condition is not evaluated. Never logs, never aborts in Release.
#ifndef RTC_BASE_CHECKS_H_
#define RTC_BASE_CHECKS_H_

#include <cassert>

#ifdef NDEBUG
#define RTC_DCHECK_IS_ON 0
#define RTC_NSP_CHECK(condition) ((void) sizeof (!(condition)))
#else
#define RTC_DCHECK_IS_ON 1
#define RTC_NSP_CHECK(condition) assert (condition)
#endif

#define RTC_CHECK(condition) RTC_NSP_CHECK (condition)
#define RTC_CHECK_EQ(a, b) RTC_NSP_CHECK ((a) == (b))
#define RTC_CHECK_NE(a, b) RTC_NSP_CHECK ((a) != (b))
#define RTC_CHECK_LE(a, b) RTC_NSP_CHECK ((a) <= (b))
#define RTC_CHECK_LT(a, b) RTC_NSP_CHECK ((a) < (b))
#define RTC_CHECK_GE(a, b) RTC_NSP_CHECK ((a) >= (b))
#define RTC_CHECK_GT(a, b) RTC_NSP_CHECK ((a) > (b))
#define RTC_CHECK_NOTREACHED() RTC_NSP_CHECK (false)

#define RTC_DCHECK(condition) RTC_NSP_CHECK (condition)
#define RTC_DCHECK_EQ(a, b) RTC_NSP_CHECK ((a) == (b))
#define RTC_DCHECK_NE(a, b) RTC_NSP_CHECK ((a) != (b))
#define RTC_DCHECK_LE(a, b) RTC_NSP_CHECK ((a) <= (b))
#define RTC_DCHECK_LT(a, b) RTC_NSP_CHECK ((a) < (b))
#define RTC_DCHECK_GE(a, b) RTC_NSP_CHECK ((a) >= (b))
#define RTC_DCHECK_GT(a, b) RTC_NSP_CHECK ((a) > (b))
#define RTC_DCHECK_NOTREACHED() RTC_NSP_CHECK (false)

#endif // RTC_BASE_CHECKS_H_
