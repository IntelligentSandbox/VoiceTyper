#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Base layer type aliases
//
// Fixed-width aliases with short, unambiguous names, following Rust's numeric
// naming. These are plain aliases, not distinct types: u64 *is* uint64_t, so
// everything interops with library APIs at zero cost. There is intentionally
// no alias for long double (width is platform-dependent).
// ---------------------------------------------------------------------------

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;
