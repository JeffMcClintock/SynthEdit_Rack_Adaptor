// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// A MOCK of VCV Rack's plugin.hpp.
//
// PURPOSE: let VCV Rack modules' own .cpp files compile, unmodified, inside a
// GMPI plugin. Nothing here is a working implementation of Rack — it is the
// smallest set of declarations that satisfies the compiler, so that porting
// becomes "replace one stub at a time with a GMPI-backed implementation"
// rather than "rewrite the module".
//
// WHAT IS REAL vs WHAT IS A STUB
//   Real  — simd::float_4 arithmetic, clamp, sqrt, Param/Port value storage,
//           Port::isConnected(), and the config*() metadata capture that
//           RackAdaptor.h reads to generate GMPI XML. These are small and
//           exact; faking them would be no less code and would have to be
//           undone later.
//   Stub  — everything host-side: widgets, panels, menus, json, asset paths.
//           They compile and do nothing. Each is marked MOCK.
//
// Scope: driven by what the ported Fundamental modules actually reference.
// Other modules will need more; add it here as they are ported.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <complex>
#include <fstream>
#include <functional>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// jansson — Rack persists module state through it. Opaque and inert here, so
// dataToJson()/dataFromJson() compile but store nothing. In C, hence global
// namespace and C-style names.
// ---------------------------------------------------------------------------
struct json_t;

inline json_t* json_object() { return nullptr; }                            // MOCK
inline json_t* json_integer(long long) { return nullptr; }                  // MOCK
inline long long json_integer_value(json_t*) { return 0; }                  // MOCK
inline json_t* json_boolean(bool) { return nullptr; }                       // MOCK
inline bool json_boolean_value(json_t*) { return false; }                   // MOCK
inline json_t* json_real(double) { return nullptr; }                        // MOCK
inline double json_real_value(json_t*) { return 0.0; }                      // MOCK
inline json_t* json_object_get(json_t*, const char*) { return nullptr; }    // MOCK
inline void json_object_update(json_t*, json_t*) {}                         // MOCK
inline void json_object_set(json_t*, const char*, json_t*) {}               // MOCK
inline void json_decref(json_t*) {}                                         // MOCK
inline json_t* json_incref(json_t* j) { return j; }                         // MOCK
inline void json_object_set_new(json_t*, const char*, json_t*) {}           // MOCK
inline double json_number_value(json_t*) { return 0.0; }                    // MOCK
inline bool json_is_true(json_t*) { return false; }                         // MOCK
inline json_t* json_array() { return nullptr; }                             // MOCK
inline json_t* json_array_get(json_t*, size_t) { return nullptr; }          // MOCK
inline size_t json_array_size(json_t*) { return 0; }                        // MOCK
inline void json_array_insert_new(json_t*, size_t, json_t*) {}              // MOCK
inline void json_array_append_new(json_t*, json_t*) {}                      // MOCK
inline json_t* json_string(const char*) { return nullptr; }                 // MOCK
inline const char* json_string_value(json_t*) { return ""; }                // MOCK

// Rack's enum-block helper: ENUMS(IN_INPUTS, 12) declares IN_INPUTS plus a
// _LAST entry so the following enumerator lands after the whole block.
#define ENUMS(name, count) name, name##_LAST = name + (count) - 1

// GLFW input constants. Rack pulls these from glfw3.h; only the ones
// Fundamental's widgets test against are listed, with GLFW's own values.
enum {
	GLFW_RELEASE = 0, GLFW_PRESS = 1, GLFW_REPEAT = 2,
	GLFW_MOUSE_BUTTON_LEFT = 0, GLFW_MOUSE_BUTTON_RIGHT = 1, GLFW_MOUSE_BUTTON_MIDDLE = 2,
	GLFW_MOD_SHIFT = 0x0001, GLFW_MOD_CONTROL = 0x0002, GLFW_MOD_ALT = 0x0004, GLFW_MOD_SUPER = 0x0008,
	GLFW_KEY_ESCAPE = 256, GLFW_KEY_ENTER = 257, GLFW_KEY_TAB = 258, GLFW_KEY_BACKSPACE = 259,
	GLFW_KEY_DELETE = 261, GLFW_KEY_RIGHT = 262, GLFW_KEY_LEFT = 263,
	GLFW_KEY_DOWN = 264, GLFW_KEY_UP = 265,
	GLFW_KEY_0 = 48, GLFW_KEY_A = 65, GLFW_KEY_C = 67, GLFW_KEY_V = 86, GLFW_KEY_X = 88,
};

namespace rack {

// Rack's scope-exit macro, spelled the same way so module source that uses it
// compiles unchanged: DEFER({ some_cleanup(); });
namespace detail {
template<class F> struct DeferHolder
{
	F f;
	~DeferHolder() { f(); }
};
template<class F> DeferHolder<F> makeDefer(F f) { return { f }; }
}
#define RACK_DEFER_CAT2(a, b) a##b
#define RACK_DEFER_CAT(a, b) RACK_DEFER_CAT2(a, b)
#define DEFER(code) auto RACK_DEFER_CAT(rackDefer, __LINE__) = ::rack::detail::makeDefer([&]() code)


// ---------------------------------------------------------------------------
// math
// ---------------------------------------------------------------------------
struct Vec
{
	float x{}, y{};
	Vec() = default;
	Vec(float x, float y) : x(x), y(y) {}

	Vec plus (Vec b) const { return { x + b.x, y + b.y }; }
	Vec minus(Vec b) const { return { x - b.x, y - b.y }; }
	Vec mult (float k) const { return { x * k, y * k }; }
	Vec mult (Vec b) const { return { x * b.x, y * b.y }; }
	Vec div  (float k) const { return { x / k, y / k }; }
	Vec div  (Vec b) const { return { x / b.x, y / b.y }; }
	Vec neg  () const { return { -x, -y }; }
	bool isZero() const { return x == 0.f && y == 0.f; }
};

inline Vec operator+(Vec a, Vec b) { return { a.x + b.x, a.y + b.y }; }
inline Vec operator-(Vec a, Vec b) { return { a.x - b.x, a.y - b.y }; }
inline Vec operator*(Vec a, float k) { return { a.x * k, a.y * k }; }
inline Vec operator/(Vec a, float k) { return { a.x / k, a.y / k }; }

// Componentwise, which is how a widget maps a normalised point into its own
// box: scopeRect.pos + scopeRect.size * p.
inline Vec operator*(Vec a, Vec b) { return { a.x * b.x, a.y * b.y }; }
inline Vec operator/(Vec a, Vec b) { return { a.x / b.x, a.y / b.y }; }

struct Rect
{
	Vec pos, size;

	Rect() = default;
	Rect(Vec p, Vec s) : pos(p), size(s) {}

	static Rect fromMinMax(Vec a, Vec b) { return { a, b - a }; }

	float getRight()  const { return pos.x + size.x; }
	float getBottom() const { return pos.y + size.y; }
	Vec   getCenter() const { return { pos.x + size.x * 0.5f, pos.y + size.y * 0.5f }; }
	Vec   getTopLeft() const { return pos; }
	Vec   getBottomRight() const { return { getRight(), getBottom() }; }

	// Point at (t.x, t.y) across the rect, 0..1.
	Vec interpolate(Vec t) const { return { pos.x + size.x * t.x, pos.y + size.y * t.y }; }

	bool contains(Vec v) const
	{ return v.x >= pos.x && v.x < getRight() && v.y >= pos.y && v.y < getBottom(); }

	Rect grow(Vec d) const { return { pos - d, size + d * 2.f }; }
	Rect shrink(Vec d) const { return { pos + d, size - d * 2.f }; }
	Rect zeroPos() const { return { Vec(0.f, 0.f), size }; }
	Vec  getBottomLeft() const { return { pos.x, getBottom() }; }
	Vec  getTopRight() const { return { getRight(), pos.y }; }
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

// Rack exposes the polyphony limit at namespace scope as well as on Port.
static constexpr int PORT_MAX_CHANNELS = 16;

inline float clamp(float x, float lo, float hi)
{
	return std::min(std::max(x, lo), hi);
}

inline float clampSafe(float x, float lo, float hi)
{ return (lo <= hi) ? clamp(x, lo, hi) : clamp(x, hi, lo); }

inline float rescale(float x, float xMin, float xMax, float yMin, float yMax)
{
	const float span = xMax - xMin;
	return yMin + (span != 0.f ? (x - xMin) / span * (yMax - yMin) : 0.f);
}

inline float crossfade(float a, float b, float p) { return a + (b - a) * p; }
inline float sgn(float x) { return (x > 0.f) ? 1.f : ((x < 0.f) ? -1.f : 0.f); }
inline int   sgn(int x)   { return (x > 0) ? 1 : ((x < 0) ? -1 : 0); }
inline float normalizeZero(float x) { return (x == 0.f) ? 0.f : x; }

inline int eucDiv(int a, int b) { const int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
inline int eucMod(int a, int b) { const int r = a % b; return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r; }

inline bool isNear(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) <= eps; }

inline float interpolateLinear(const float* p, float x)
{
	const int xi = (int)x;
	const float xf = x - xi;
	return crossfade(p[xi], p[xi + 1], xf);
}

// Rack's RNG. Real, because a module using noise wants noise, not silence.
namespace random {
	inline unsigned int& state() { static unsigned int s = 0x12345678u; return s; }

	inline unsigned int u32()
	{
		unsigned int& x = state();
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		return x;
	}

	inline float uniform() { return (u32() >> 8) * (1.0f / 16777216.0f); }   // [0,1)

	inline float normal()
	{
		// Box-Muller
		const float u1 = (std::max)(uniform(), 1e-7f);
		const float u2 = uniform();
		return std::sqrt(-2.f * std::log(u1)) * std::cos(2.f * (float)M_PI * u2);
	}

	template<typename T> T get();
	template<> inline float get<float>() { return uniform(); }
	template<> inline bool  get<bool>()  { return (u32() & 1u) != 0u; }

	inline void init() {}
}

namespace math {

// Integer log2, rounded down. Rack has this alongside the float overload; the
// wavetable derives its octave count from the wave length with it.
inline int log2(int n)
{
	int r = 0;
	while (n >>= 1) ++r;
	return r;
}

	using Vec = ::rack::Vec;
	using Rect = ::rack::Rect;
	using rack::clamp;
	using rack::clampSafe;
	using rack::rescale;
	using rack::crossfade;
	using rack::sgn;
	using rack::eucDiv;
	using rack::eucMod;
	using rack::isNear;
	using rack::normalizeZero;
	using rack::interpolateLinear;
}

// ---------------------------------------------------------------------------
// simd — Rack's 4-wide float. REAL: a plain array with the operators the
// Fundamental modules need. Scalar, not vectorised; correctness first, and
// the compiler auto-vectorises this shape well anyway.
// ---------------------------------------------------------------------------
namespace simd {

struct float_4
{
	float v[4]{};

	float_4() = default;
	float_4(float a) { v[0] = v[1] = v[2] = v[3] = a; }          // implicit: modules assign a float to a float_4
	float_4(float a, float b, float c, float d) { v[0] = a; v[1] = b; v[2] = c; v[3] = d; }

	float&       operator[](int i)       { return v[i]; }
	const float& operator[](int i) const { return v[i]; }

	// An all-lanes-set mask, as modules use for "gate is on in every voice".
	static float_4 mask();

	static float_4 load(const float* p) { return { p[0], p[1], p[2], p[3] }; }
	static float_4 zero() { return float_4(0.f); }
	void store(float* p) const { for (int i = 0; i < 4; ++i) p[i] = v[i]; }
};

#define RACK_FLOAT4_BINOP(op)                                                       \
	inline float_4 operator op (float_4 a, float_4 b)                                   \
	{ return { a[0] op b[0], a[1] op b[1], a[2] op b[2], a[3] op b[3] }; }

RACK_FLOAT4_BINOP(+)
RACK_FLOAT4_BINOP(-)
RACK_FLOAT4_BINOP(*)
RACK_FLOAT4_BINOP(/)
#undef RACK_FLOAT4_BINOP

inline float_4& operator+=(float_4& a, float_4 b) { a = a + b; return a; }
inline float_4& operator-=(float_4& a, float_4 b) { a = a - b; return a; }
inline float_4& operator*=(float_4& a, float_4 b) { a = a * b; return a; }

// Comparisons yield a mask float_4 (all-ones / zero per lane), which is what
// ifelse() and movemask() consume — the same shape as a real SIMD register.
inline float_4 maskOf(bool b)
{
	float_4 r;
	const uint32_t bits = b ? 0xFFFFFFFFu : 0u;
	for (int i = 0; i < 4; ++i) std::memcpy(&r[i], &bits, sizeof(float));
	return r;
}

inline float_4 float_4::mask() { return maskOf(true); }

inline bool laneSet(float x)
{
	uint32_t bits{};
	std::memcpy(&bits, &x, sizeof(bits));
	return bits != 0u;
}

#define RACK_FLOAT4_CMP(op)                                                    \
	inline float_4 operator op (float_4 a, float_4 b)                          \
	{ return { maskOf(a[0] op b[0])[0], maskOf(a[1] op b[1])[1],                \
	           maskOf(a[2] op b[2])[2], maskOf(a[3] op b[3])[3] }; }

RACK_FLOAT4_CMP(<)
RACK_FLOAT4_CMP(<=)
RACK_FLOAT4_CMP(>)
RACK_FLOAT4_CMP(>=)
RACK_FLOAT4_CMP(==)
RACK_FLOAT4_CMP(!=)
#undef RACK_FLOAT4_CMP

inline float_4 operator|(float_4 a, float_4 b)
{
	float_4 r;
	for (int i = 0; i < 4; ++i) r[i] = laneSet(a[i]) ? a[i] : b[i];
	return r;
}

inline float_4 operator^(float_4 a, float_4 b)
{
	float_4 r;
	for (int i = 0; i < 4; ++i) r[i] = (laneSet(a[i]) != laneSet(b[i])) ? maskOf(true)[i] : 0.f;
	return r;
}

inline float_4 operator~(float_4 a)
{
	float_4 r;
	for (int i = 0; i < 4; ++i) r[i] = laneSet(a[i]) ? 0.f : maskOf(true)[i];
	return r;
}

inline float_4 operator&(float_4 a, float_4 b)
{
	float_4 r;
	for (int i = 0; i < 4; ++i) r[i] = (laneSet(a[i]) && laneSet(b[i])) ? a[i] : 0.f;
	return r;
}

inline float_4 operator-(float_4 a) { return { -a[0], -a[1], -a[2], -a[3] }; }

inline float_4& operator|=(float_4& a, float_4 b) { a = a | b; return a; }
inline float_4& operator&=(float_4& a, float_4 b) { a = a & b; return a; }
inline float_4& operator^=(float_4& a, float_4 b) { a = a ^ b; return a; }
inline float_4& operator/=(float_4& a, float_4 b) { a = a / b; return a; }

// Per-lane select: mask lane set -> a, else b.
inline float_4 ifelse(float_4 mask, float_4 a, float_4 b)
{
	float_4 r;
	for (int i = 0; i < 4; ++i) r[i] = laneSet(mask[i]) ? a[i] : b[i];
	return r;
}

// Turn a lane bitmask back into a vector mask — the inverse of movemask(),
// spelled as a template because that is how Rack has it:
// simd::movemaskInverse<float_4>(1 << c).
template<typename T>
inline T movemaskInverse(int bits)
{
	T r;
	for (int i = 0; i < 4; ++i) r[i] = maskOf((bits & (1 << i)) != 0)[0];
	return r;
}

// Bit i set when lane i's mask is set.
inline int movemask(float_4 mask)
{
	int m = 0;
	for (int i = 0; i < 4; ++i) if (laneSet(mask[i])) m |= (1 << i);
	return m;
}

#define RACK_FLOAT4_UNARY(name, expr)                                          \
	inline float_4 name(float_4 a)                                             \
	{ return { expr(a[0]), expr(a[1]), expr(a[2]), expr(a[3]) }; }

RACK_FLOAT4_UNARY(fabs,  std::fabs)
RACK_FLOAT4_UNARY(floor, std::floor)
RACK_FLOAT4_UNARY(ceil,  std::ceil)
RACK_FLOAT4_UNARY(round, std::round)
RACK_FLOAT4_UNARY(trunc, std::trunc)
RACK_FLOAT4_UNARY(sin,   std::sin)
RACK_FLOAT4_UNARY(cos,   std::cos)
RACK_FLOAT4_UNARY(log2,  std::log2)
RACK_FLOAT4_UNARY(exp2,  std::exp2)
#undef RACK_FLOAT4_UNARY

inline float_4 fmax(float_4 a, float_4 b)
{ return { (std::max)(a[0],b[0]), (std::max)(a[1],b[1]), (std::max)(a[2],b[2]), (std::max)(a[3],b[3]) }; }

inline float_4 fmin(float_4 a, float_4 b)
{ return { (std::min)(a[0],b[0]), (std::min)(a[1],b[1]), (std::min)(a[2],b[2]), (std::min)(a[3],b[3]) }; }

inline float_4 pow(float_4 a, float_4 b)
{ return { std::pow(a[0],b[0]), std::pow(a[1],b[1]), std::pow(a[2],b[2]), std::pow(a[3],b[3]) }; }

inline float_4 pow(float a, float_4 b)  { return pow(float_4(a), b); }

inline float_4 rescale(float_4 x, float_4 xMin, float_4 xMax, float_4 yMin, float_4 yMax)
{
	float_4 r;
	for (int i = 0; i < 4; ++i)
	{
		const float span = xMax[i] - xMin[i];
		r[i] = yMin[i] + (span != 0.f ? (x[i] - xMin[i]) / span * (yMax[i] - yMin[i]) : 0.f);
	}
	return r;
}

inline float_4 rcp(float_4 a)
{ return { 1.f/a[0], 1.f/a[1], 1.f/a[2], 1.f/a[3] }; }

inline float_4 rsqrt(float_4 a)
{ return { 1.f/std::sqrt(a[0]), 1.f/std::sqrt(a[1]), 1.f/std::sqrt(a[2]), 1.f/std::sqrt(a[3]) }; }

inline float_4 sqrt(float_4 a)
{
	return { std::sqrt(a[0]), std::sqrt(a[1]), std::sqrt(a[2]), std::sqrt(a[3]) };
}

// Found by ADL from a module's unqualified clamp() on a float_4. The bounds
// arrive both ways: VCF writes simd::clamp(u0, T(-4), T(4)) with T deduced as
// float_4, so the vector-bound overload is not optional.
inline float_4 clamp(float_4 x, float lo, float hi)
{
	return { rack::clamp(x[0], lo, hi), rack::clamp(x[1], lo, hi),
	         rack::clamp(x[2], lo, hi), rack::clamp(x[3], lo, hi) };
}

// Rack defaults the bounds to 0..1, and the wavetable modules rely on it:
// simd::clamp(x) with no bounds at all.
inline float_4 clamp(float_4 x) { return clamp(x, 0.f, 1.f); }

inline float_4 clamp(float_4 x, float_4 lo, float_4 hi)
{
	return { rack::clamp(x[0], lo[0], hi[0]), rack::clamp(x[1], lo[1], hi[1]),
	         rack::clamp(x[2], lo[2], hi[2]), rack::clamp(x[3], lo[3], hi[3]) };
}

inline float_4 fmod(float_4 a, float_4 b)
{ return { std::fmod(a[0],b[0]), std::fmod(a[1],b[1]), std::fmod(a[2],b[2]), std::fmod(a[3],b[3]) }; }

inline float_4 tan(float_4 a)  { return { std::tan(a[0]),  std::tan(a[1]),  std::tan(a[2]),  std::tan(a[3])  }; }
inline float_4 atan(float_4 a) { return { std::atan(a[0]), std::atan(a[1]), std::atan(a[2]), std::atan(a[3]) }; }
inline float_4 tanh(float_4 a) { return { std::tanh(a[0]), std::tanh(a[1]), std::tanh(a[2]), std::tanh(a[3]) }; }
inline float_4 exp(float_4 a)  { return { std::exp(a[0]),  std::exp(a[1]),  std::exp(a[2]),  std::exp(a[3])  }; }
inline float_4 log(float_4 a)  { return { std::log(a[0]),  std::log(a[1]),  std::log(a[2]),  std::log(a[3])  }; }
inline float_4 log10(float_4 a){ return { std::log10(a[0]),std::log10(a[1]),std::log10(a[2]),std::log10(a[3])}; }

inline float_4 atan2(float_4 a, float_4 b)
{ return { std::atan2(a[0],b[0]), std::atan2(a[1],b[1]), std::atan2(a[2],b[2]), std::atan2(a[3],b[3]) }; }

inline float_4 sgn(float_4 a)
{ return { ::rack::sgn(a[0]), ::rack::sgn(a[1]), ::rack::sgn(a[2]), ::rack::sgn(a[3]) }; }

inline float_4 crossfade(float_4 a, float_4 b, float_4 p) { return a + (b - a) * p; }

// The scalar forms of all of the above. Fundamental writes its DSP generically
//
//     template <typename T> T blackmanHarris(T p)
//     { return T(0.35875) - T(0.48829) * simd::cos(T(2 * M_PI) * p) + ...; }
//
// and instantiates it at BOTH float_4 and double. Without these, a double
// argument would silently convert to float_4 and the function would try to
// return a vector where a scalar was promised.
using std::fabs;  using std::floor; using std::ceil;  using std::round;
using std::trunc; using std::sin;   using std::cos;   using std::tan;
using std::atan;  using std::atan2; using std::tanh;  using std::exp;
using std::log;   using std::log2;  using std::log10; using std::exp2;
using std::pow;   using std::sqrt;  using std::fmod;
using ::rack::clamp;
using ::rack::rescale;
using ::rack::sgn;
using ::rack::crossfade;

} // namespace simd

// ---------------------------------------------------------------------------
// dsp — small helpers modules keep as members. REAL: they are a few lines each
// and stubbing them would give modules wrong behaviour rather than none.
// ---------------------------------------------------------------------------
namespace dsp {

// Divides the sample clock, so lights and other slow work run every Nth frame.
struct ClockDivider
{
	uint32_t clock{}, division{ 1 };

	void setDivision(uint32_t d) { division = d ? d : 1; }
	uint32_t getDivision() const { return division; }

	bool process()
	{
		if (++clock >= division) { clock = 0; return true; }
		return false;
	}
};

// Peak meter with exponential decay, as Rack's. Feeds light brightness, so
// nothing audible depends on it — but wrong is worse than absent.
struct VuMeter2
{
	enum Mode { PEAK, RMS };

	Mode  mode{ PEAK };
	float v{};
	float lambda{ 30.f };   // decay rate, /second

	void process(float deltaTime, float value)
	{
		value = std::fabs(value);

		if (value >= v)
			v = value;                                  // attack instantly
		else
			v += (value - v) * lambda * deltaTime;       // decay
	}

	// bMin/bMax are dB thresholds; returns 0..1 across that span.
	float getBrightness(float bMin, float bMax) const
	{
		if (v <= 0.f)
			return 0.f;

		const float db = 20.f * std::log10(v);

		if (db <= bMin) return 0.f;
		if (db >= bMax) return 1.f;

		return (bMax > bMin) ? (db - bMin) / (bMax - bMin) : 1.f;
	}
};

// Constants Rack exposes for pitch conversion.
static constexpr float FREQ_C4 = 261.6256f;
static constexpr float FREQ_C  = FREQ_C4;
static constexpr float FREQ_SEMITONE = 1.0594631f;

inline float exp2_taylor5(float x) { return std::exp2(x); }
inline simd::float_4 exp2_taylor5(simd::float_4 x) { return simd::exp2(x); }
inline float exp(float x)          { return std::exp(x); }

// Fires once when the input crosses from low to high.
template<typename T = float>
struct TSchmittTrigger
{
	// Vector form: `state` is a per-lane mask, and process() returns a mask of
	// the lanes that triggered this sample. ADSR and RandomValues instantiate
	// this at float_4, where `if (in <= threshold)` has no meaning — the four
	// lanes can disagree. The float specialization below keeps the bool API.
	T state;

	TSchmittTrigger() { reset(); }

	void reset() { state = T::mask(); }

	T process(T in, T lowThreshold = 0.f, T highThreshold = 1.f)
	{
		T on  = (in >= highThreshold);
		T off = (in <= lowThreshold);
		T triggered = ~state & on;
		state = on | (state & ~off);
		return triggered;
	}

	T isHigh() const { return state; }
};
// The scalar case, which most of the set uses. Rack specializes it the same
// way: with one lane there is nothing to mask, so the state and the return are
// plain bools and `if (trigger.process(x))` reads naturally.
template<>
struct TSchmittTrigger<float>
{
	bool state{ true };

	void reset() { state = true; }

	bool process(float in, float lowThreshold = 0.f, float highThreshold = 1.f)
	{
		if (state)
		{
			if (in <= lowThreshold)
				state = false;
		}
		else if (in >= highThreshold)
		{
			state = true;
			return true;
		}
		return false;
	}

	bool isHigh() const { return state; }
};

using SchmittTrigger = TSchmittTrigger<float>;

// Fires once on a false -> true transition.
struct BooleanTrigger
{
	bool state{ true };

	bool process(bool in)
	{
		const bool triggered = in && !state;
		state = in;
		return triggered;
	}

	bool isHigh() const { return state; }
	void reset() { state = true; }
};

// Holds an output high for a set time after being triggered.
struct PulseGenerator
{
	float remaining{};

	bool process(float deltaTime)
	{
		if (remaining > 0.f) { remaining -= deltaTime; return true; }
		return false;
	}

	void trigger(float duration = 1e-3f) { if (duration > remaining) remaining = duration; }
	void reset() { remaining = 0.f; }
};

// Counts up, so a module can ask how long since something happened.
struct Timer
{
	float time{};

	float process(float deltaTime) { time += deltaTime; return time; }
	float getTime() const { return time; }
	void  reset() { time = 0.f; }
};

// Limits how fast the output can follow the input.
struct SlewLimiter
{
	float out{}, rise{ 1.f }, fall{ 1.f };

	void setRiseFall(float r, float f) { rise = r; fall = f; }

	float process(float deltaTime, float in)
	{
		const float limit = (in > out) ? rise : fall;
		const float delta = clamp(in - out, -fall * deltaTime, rise * deltaTime);
		(void)limit;
		out += delta;
		return out;
	}

	void reset() { out = 0.f; }
};

struct ExponentialSlewLimiter : SlewLimiter {};

// One-pole filter. Rack's RCFilter with the same lowpass/highpass outputs.
template<typename T = float>
struct TRCFilter
{
	T c{ 0.f }, xstate[1]{}, ystate[1]{};

	void setCutoff(T r)       { c = 2.f / r; }
	void setCutoffFreq(T r)   { setCutoff(2.f * (T)3.14159265358979f * r); }

	void process(T x)
	{
		const T y = (x + xstate[0] - ystate[0] * (1.f - c)) / (1.f + c);
		xstate[0] = x;
		ystate[0] = y;
	}

	T lowpass()  const { return ystate[0]; }
	T highpass() const { return xstate[0] - ystate[0]; }
	void reset() { xstate[0] = ystate[0] = T(0); }
};
using RCFilter = TRCFilter<float>;

// MOCK: enough shape to compile. Modules using these produce no output rather
// than wrong output — see the unsupported list in the README.
// REAL: direct-form-1 IIR, in Rack's arrangement and coefficient convention —
// b has B_ORDER taps, a has A_ORDER-1 (a[0] is taken as 1). Noise runs its red
// output through one of these, so the pass-through mock this replaces was
// emitting white noise under a red label.
template<int B_ORDER, int A_ORDER = B_ORDER, typename T = float>
struct IIRFilter
{
	T b[B_ORDER]{};
	T a[A_ORDER > 1 ? A_ORDER - 1 : 1]{};
	T x[B_ORDER > 1 ? B_ORDER - 1 : 1]{};
	T y[A_ORDER > 1 ? A_ORDER - 1 : 1]{};

	IIRFilter() { reset(); }

	void setCoefficients(const T* bIn, const T* aIn)
	{
		for (int i = 0; i < B_ORDER; ++i) b[i] = bIn[i];
		for (int i = 1; i < A_ORDER; ++i) a[i - 1] = aIn[i - 1];
	}

	void reset()
	{
		for (int i = 0; i < B_ORDER - 1; ++i) x[i] = T(0);
		for (int i = 0; i < A_ORDER - 1; ++i) y[i] = T(0);
	}

	T process(T in)
	{
		T out = b[0] * in;

		for (int i = 1; i < B_ORDER; ++i) out += b[i] * x[i - 1];
		for (int i = 1; i < A_ORDER; ++i) out -= a[i - 1] * y[i - 1];

		for (int i = B_ORDER - 2; i > 0; --i) x[i] = x[i - 1];
		if (B_ORDER > 1) x[0] = in;

		for (int i = A_ORDER - 2; i > 0; --i) y[i] = y[i - 1];
		if (A_ORDER > 1) y[0] = out;

		return out;
	}
};

template<typename T = float>
struct TBiquadFilter : IIRFilter<3, 3, T>
{
	enum Type { LOWPASS, HIGHPASS, BANDPASS, NOTCH, PEAK, LOWSHELF, HIGHSHELF, NUM_TYPES };
	void setParameters(Type, float, float, float) {}   // MOCK: nothing in this set calls it
};
using BiquadFilter = TBiquadFilter<>;

template<int Z = 16, int O = 32, typename T = float>
struct MinBlepGenerator
{
	T buf[2 * Z]{};
	int pos{};
	void insertDiscontinuity(T, T) {}   // MOCK
	T process() { return T(0); }        // MOCK
};

template<typename T, size_t S>
struct DoubleRingBuffer
{
	T data[S * 2]{};
	size_t start{}, end{};

	void push(T t) { data[end % (S * 2)] = t; ++end; }
	T shift() { return data[start++ % (S * 2)]; }
	void clear() { start = end = 0; }
	size_t size() const { return end - start; }
	bool empty() const { return start == end; }
	bool full() const { return size() >= S; }
	T* endData() { return &data[end % (S * 2)]; }
	T* startData() { return &data[start % (S * 2)]; }

	// Contiguous room at each end. Delay hands endData()/capacity() straight to
	// src_process() as an output buffer, then advances by what was produced —
	// so these have to describe the run that is safe to write, not the total.
	size_t capacity() const { return S * 2 - (end % (S * 2)); }
	size_t startCapacity() const { return S * 2 - (start % (S * 2)); }

	void startIncr(size_t n) { start += n; }
	void endIncr(size_t n) { end += n; }
};

// Rack's sample-format conversion: integer types are normalised against their
// full scale, float passes through. REAL, not mocked — it is pure arithmetic,
// and the wavetable loader reads .s8/.i16/.f32 files through it.
template<typename T>
inline float convertToFloat(T x)
{
	if constexpr (std::is_floating_point_v<T>)
		return (float)x;
	else if constexpr (std::is_signed_v<T>)
		return (float)x / (float)(-(std::numeric_limits<T>::min)());
	else
		return (float)x / (float)(std::numeric_limits<T>::max)() * 2.f - 1.f;
}

// A 24-bit sample, three bytes wide with no padding, little-endian — the
// wavetable loader sizes a .s24 file with sizeof(Int24), so 3 is load-bearing.
// Stored as bytes rather than a bitfield because a bitfield's width is a
// compiler's choice and this one has to be exactly three.
#pragma pack(push, 1)
struct Int24
{
	uint8_t v[3];

	int32_t value() const
	{
		const int32_t raw = (int32_t)v[0] | ((int32_t)v[1] << 8) | ((int32_t)v[2] << 16);
		return (raw & 0x800000) ? (raw | ~0xFFFFFF) : raw;   // sign-extend
	}
};
#pragma pack(pop)
static_assert(sizeof(Int24) == 3, "Int24 must be exactly three bytes");

inline float convertToFloat(Int24 x) { return (float)x.value() / 8388608.f; }

template<typename T>
inline void convert(const T* in, float* out, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		out[i] = convertToFloat(in[i]);
}

// float-to-T only. Without the guard, converting float to float matches both
// overloads equally and the call is ambiguous — which is exactly what the .f32
// branch of the wavetable loader does.
template<typename T, typename = std::enable_if_t<!std::is_same_v<T, float>>>
inline void convert(const float* in, T* out, size_t len)
{
	for (size_t i = 0; i < len; ++i)
	{
		if constexpr (std::is_floating_point_v<T>)
			out[i] = (T)in[i];
		else
		{
			const float scale = (float)(-(std::numeric_limits<T>::min)());
			const float v = in[i] * scale;
			out[i] = (T)::rack::clamp(v, (float)(std::numeric_limits<T>::min)(),
			                             (float)(std::numeric_limits<T>::max)());
		}
	}
}

struct VuMeter : VuMeter2 {};

// MOCK: FFT is only used for display in this set.
struct RealFFT
{
	size_t length{};
	RealFFT(size_t n) : length(n) {}
	void rfft(const float*, float*) {}
	void irfft(const float*, float*) {}
	void scan(const float*, float*, size_t) {}
};

} // namespace dsp

// ---------------------------------------------------------------------------
// string — Rack's printf helper
// ---------------------------------------------------------------------------
namespace string {

inline std::string f(const char* fmt, ...)
{
	char buf[256];
	va_list args;
	va_start(args, fmt);
	const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	return (n > 0) ? std::string(buf, (std::min)(n, (int)sizeof(buf) - 1)) : std::string();
}


// Rack compares plugin versions when migrating patches. MOCK: everything
// compares equal, which is what a fresh port wants.
struct Version
{
	Version() = default;
	Version(const std::string& s) : text(s) {}
	// json_string_value() hands back a const char*, and reaching Version
	// through std::string would be two user-defined conversions.
	Version(const char* s) : text(s ? s : "") {}
	std::string text;
	bool operator<(const Version&) const { return false; }
	bool operator>(const Version&) const { return false; }
	bool operator==(const Version& o) const { return text == o.text; }
};

inline std::string lowercase(const std::string& in)
{
	std::string out = in;
	for (auto& c : out) c = (char)std::tolower((unsigned char)c);
	return out;
}

inline std::string uppercase(const std::string& in)
{
	std::string out = in;
	for (auto& c : out) c = (char)std::toupper((unsigned char)c);
	return out;
}

inline std::string trim(const std::string& in)
{
	const auto first = in.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return {};

	return in.substr(first, in.find_last_not_of(" \t\r\n") - first + 1);
}

// Rack uses these to hand a path to a Windows wide-char API. Narrow-to-wide
// here is a plain widening cast, which is right for ASCII paths and wrong for
// anything above U+007F — acceptable only because the one caller
// (Wavetable.hpp) is on a path that compat/osdialog.h never reaches.
inline std::wstring UTF8toUTF16(const std::string& in)
{
	return std::wstring(in.begin(), in.end());
}

inline std::string UTF16toUTF8(const std::wstring& in)
{
	return std::string(in.begin(), in.end());
}

} // namespace string

// ---------------------------------------------------------------------------
// system — path and file helpers
// ---------------------------------------------------------------------------
namespace system {

inline std::string getFilename(const std::string& path)
{
	const auto slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline std::string getDirectory(const std::string& path)
{
	const auto slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

inline std::string getExtension(const std::string& path)
{
	const std::string name = getFilename(path);
	const auto dot = name.find_last_of('.');
	return dot == std::string::npos ? std::string{} : name.substr(dot);
}

inline std::string getStem(const std::string& path)
{
	const std::string name = getFilename(path);
	const auto dot = name.find_last_of('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

inline std::string join(const std::string& a, const std::string& b)
{
	if (a.empty()) return b;
	if (b.empty()) return a;
	return a + "/" + b;
}

// Bytes, not text: Rack returns std::vector<uint8_t> and the wavetable loader
// reinterprets the buffer as int8/int16/float samples.
inline std::vector<uint8_t> readFile(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return {};

	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

inline bool exists(const std::string& path)
{
	std::ifstream f(path);
	return (bool)f;
}

} // namespace system

// ---------------------------------------------------------------------------
// engine
// ---------------------------------------------------------------------------
struct Param
{
	float value{};
	float getValue() const   { return value; }
	void  setValue(float v)  { value = v; }
};

struct Light
{
	float value{};

	void  setBrightness(float b) { value = b; }
	float getBrightness() const  { return value; }

	// Rack's time-aware variant; the adaptor does not render lights, so the
	// smoothing is not modelled.
	void setBrightnessSmooth(float b, float /*deltaTime*/, float /*lambda*/ = 30.f) { value = b; }
	void setSmoothBrightness(float b, float /*deltaTime*/) { value = b; }
};

struct Port
{
	static constexpr int PORT_MAX_CHANNELS = 16;

	float voltages[PORT_MAX_CHANNELS]{};
	int   channels{ 1 };
	bool  connected{ false };

	// Real, and load-bearing: modules commonly return from process()
	// immediately unless an output is connected. The adaptor marks the ports
	// it actually drives — see RackProcessor's constructor.
	//
	// CAVEAT: the adaptor drives every port, so a module never sees an
	// unconnected input. A module that normalises an unconnected port to a
	// fallback value (getNormalVoltage) will instead read our 0V. Revisit
	// per-module if that distinction starts to matter.
	bool isConnected() const { return connected; }

	int  getChannels() const     { return channels; }
	void setChannels(int n)      { channels = n; }

	float getVoltage(int channel = 0) const        { return voltages[channel]; }
	void  setVoltage(float v, int channel = 0)     { voltages[channel] = v; }

	// Rack's poly semantics: a monophonic cable broadcasts channel 0 to all.
	float getPolyVoltage(int channel) const
	{
		return channels == 1 ? getVoltage(0) : getVoltage(channel);
	}

	template<typename T>
	T getPolyVoltageSimd(int firstChannel) const
	{
		T out{};
		for (int i = 0; i < 4; ++i)
			out[i] = getPolyVoltage(std::min(firstChannel + i, PORT_MAX_CHANNELS - 1));
		return out;
	}

	float  getNormalVoltage(float normal, int channel = 0) const
	{ return isConnected() ? getVoltage(channel) : normal; }

	float  getVoltageSum() const
	{
		float sum = 0.f;
		for (int c = 0; c < channels; ++c) sum += voltages[c];
		return sum;
	}

	float* getVoltages(int firstChannel = 0) { return &voltages[firstChannel]; }
	const float* getVoltages(int firstChannel = 0) const { return &voltages[firstChannel]; }

	void readVoltages(float* v) const { for (int c = 0; c < channels; ++c) v[c] = voltages[c]; }
	void writeVoltages(const float* v) { for (int c = 0; c < channels; ++c) voltages[c] = v[c]; }
	void clearVoltages() { for (auto& v : voltages) v = 0.f; }

	bool isMonophonic() const { return channels == 1; }
	bool isPolyphonic() const { return channels > 1; }

	template<typename T>
	T getVoltageSimd(int firstChannel) const
	{
		T out{};
		for (int i = 0; i < 4; ++i)
			out[i] = getVoltage((std::min)(firstChannel + i, PORT_MAX_CHANNELS - 1));
		return out;
	}

	template<typename T>
	T getNormalVoltageSimd(T normal, int firstChannel) const
	{ return isConnected() ? getVoltageSimd<T>(firstChannel) : normal; }

	template<typename T>
	T getNormalPolyVoltageSimd(T normal, int firstChannel) const
	{ return isConnected() ? getPolyVoltageSimd<T>(firstChannel) : normal; }

	template<typename T>
	void setVoltageSimd(T value, int firstChannel)
	{
		for (int i = 0; i < 4; ++i)
		{
			const int c = firstChannel + i;
			if (c < PORT_MAX_CHANNELS)
				voltages[c] = value[i];
		}
	}
};

using Input  = Port;
using Output = Port;

// REAL, not just accepted-and-dropped: RackAdaptor.h reads this back to
// generate the GMPI <Parameters> section, so the ranges, names and defaults
// the module declares in configParam() become the plugin's metadata.
struct ParamQuantity
{
	float minValue{}, maxValue{}, defaultValue{};
	std::string name, unit;

	// Flags Rack lets a module tweak after configParam(). Recorded, not acted
	// on — nothing here randomizes or snaps.
	// Display scaling, which modules retune at runtime (8vert switches between
	// volts and percent depending on what is patched).
	float displayBase{};
	float displayMultiplier{ 1.f };
	float displayOffset{};

	bool randomizeEnabled{ true };
	bool snapEnabled{ false };
	bool smoothEnabled{ true };
	bool resetEnabled{ true };

	// Defined out of line, below Module: these read and write the live
	// Param, which is the whole point of a quantity — Octave's buttons set a
	// parameter through one.
	float getValue() const;
	void  setValue(float v);
	float getImmediateValue() const { return getValue(); }
	void  setImmediateValue(float v) { setValue(v); }
	void  reset() { setValue(defaultValue); }

	float getMinValue() const     { return minValue; }
	float getMaxValue() const     { return maxValue; }
	float getDefaultValue() const { return defaultValue; }
	// Modules subclass this to give a parameter a custom display, so the
	// accessors are virtual and it carries a back-pointer like Rack's.
	virtual ~ParamQuantity() = default;

	struct Module* module{};
	int paramId{};
	std::string description;

	std::string getLabel() const  { return name; }
	virtual float getDisplayValue() { return defaultValue * displayMultiplier + displayOffset; }
	virtual void  setDisplayValue(float) {}
	virtual std::string getUnit() { return unit; }
	std::string getDisplayValueString() { return {}; }
	std::string getString() const { return name; }
	std::string getDescription() const { return description; }
};

// configSwitch's quantity: a parameter whose positions have names.
struct SwitchQuantity : ParamQuantity
{
	std::vector<std::string> labels;
};

struct Module
{
	// Rack exposes per-port metadata objects, held BY POINTER so a module can
	// write through them after config() — `outputInfos[X]->description = "..."`
	// is the idiom throughout Fundamental.
	struct PortInfo
	{
		Module* module{};
		int type{}, portId{};
		std::string name, description;

		std::string getName() const { return name.empty() ? "Port" : name; }
		std::string getDescription() const { return description; }
		std::string getFullName() const { return getName(); }
	};
	std::vector<PortInfo*> inputInfos, outputInfos;

	struct LightInfo
	{
		Module* module{};
		int lightId{};
		std::string name, description;
		std::string getName() const { return name; }
		std::string getDescription() const { return description; }
	};
	std::vector<LightInfo*> lightInfos;

	struct ProcessArgs
	{
		float   sampleRate{ 44100.0f };
		float   sampleTime{ 1.0f / 44100.0f };
		int64_t frame{};
	};

	struct ResetEvent {};
	struct RandomizeEvent {};

	std::vector<Param>  params;
	std::vector<Input>  inputs;
	std::vector<Output> outputs;
	std::vector<Light>  lights;

	// Pointers, as Rack has them — modules index in and mutate the object
	// (8vert rewrites unit and displayMultiplier every frame).
	std::vector<ParamQuantity*> paramQuantities;

	// Captured so the adaptor can name the generated GMPI pins the way the
	// module names its ports.
	std::vector<std::string> inputNames;
	std::vector<std::string> outputNames;

	virtual ~Module()
	{
		for (auto* q : paramQuantities) delete q;
		for (auto* i : inputInfos)  delete i;
		for (auto* i : outputInfos) delete i;
		for (auto* i : lightInfos)  delete i;
	}

	void config(int numParams, int numInputs, int numOutputs, int numLights = 0)
	{
		params.resize(numParams);
		inputs.resize(numInputs);
		outputs.resize(numOutputs);
		lights.resize(numLights);
		for (auto* q : paramQuantities) delete q;
		paramQuantities.assign(numParams, nullptr);
		for (auto*& q : paramQuantities) q = new ParamQuantity;
		inputNames.resize(numInputs);
		outputNames.resize(numOutputs);

		auto rebuild = [](auto& vec, int count, auto make)
		{
			for (auto* p : vec) delete p;
			vec.clear();
			for (int i = 0; i < count; ++i) vec.push_back(make(i));
		};

		rebuild(inputInfos,  numInputs,  [this](int i) { auto* p = new PortInfo;  p->module = this; p->portId  = i; p->type = 0; return p; });
		rebuild(outputInfos, numOutputs, [this](int i) { auto* p = new PortInfo;  p->module = this; p->portId  = i; p->type = 1; return p; });
		rebuild(lightInfos,  numLights,  [this](int i) { auto* p = new LightInfo; p->module = this; p->lightId = i; return p; });
	}

	// Templated on the quantity type, as Rack has it: LFO, Process and Rescale
	// each declare a ParamQuantity subclass with a custom display and pass it
	// here — configParam<FrequencyQuantity>(FREQ_PARAM, ...). The subclass
	// REPLACES the plain quantity config() allocated, so the module's own
	// getDisplayValue() is the one that survives.
	template<class TParamQuantity = ParamQuantity>
	TParamQuantity* configParam(int paramId, float minValue, float maxValue, float defaultValue,
	                            std::string name = "", std::string unit = "",
	                            float displayBase = 0.f, float displayMultiplier = 1.f,
	                            float displayOffset = 0.f)
	{
		if (paramId < 0 || paramId >= (int)params.size())
			return nullptr;

		params[paramId].value = defaultValue;

		delete paramQuantities[paramId];
		auto* q = new TParamQuantity;
		paramQuantities[paramId] = q;

		q->module = this;
		q->paramId = paramId;
		q->minValue = minValue;
		q->maxValue = maxValue;
		q->defaultValue = defaultValue;
		q->name = std::move(name);
		q->unit = std::move(unit);
		q->displayBase = displayBase;
		q->displayMultiplier = displayMultiplier;
		q->displayOffset = displayOffset;
		return q;
	}

	// Returns the info object, because modules chain onto it:
	//     configInput(A_INPUT, "A")->description = "...";
	template<class TPortInfo = PortInfo>
	TPortInfo* configInput(int portId, std::string name = "")
	{
		if (portId < 0 || portId >= (int)inputInfos.size())
			return nullptr;

		inputNames[portId] = name;

		delete inputInfos[portId];
		auto* info = new TPortInfo;
		inputInfos[portId] = info;

		info->module = this;
		info->portId = portId;
		info->type = 0;
		info->name = std::move(name);
		return info;
	}

	template<class TPortInfo = PortInfo>
	TPortInfo* configOutput(int portId, std::string name = "")
	{
		if (portId < 0 || portId >= (int)outputInfos.size())
			return nullptr;

		outputNames[portId] = name;

		delete outputInfos[portId];
		auto* info = new TPortInfo;
		outputInfos[portId] = info;

		info->module = this;
		info->portId = portId;
		info->type = 1;
		info->name = std::move(name);
		return info;
	}

	// configSwitch is configParam plus display labels for the positions. The
	// labels are recorded but not surfaced yet; the range and default are what
	// the generated XML reads.
	template<class TSwitchQuantity = SwitchQuantity>
	TSwitchQuantity* configSwitch(int paramId, float minValue, float maxValue, float defaultValue,
	                              std::string name = "", std::vector<std::string> labels = {})
	{
		auto* q = configParam<TSwitchQuantity>(paramId, minValue, maxValue, defaultValue, std::move(name));
		if (q)
			q->labels = std::move(labels);

		return q;
	}

	template<class TSwitchQuantity = SwitchQuantity>
	TSwitchQuantity* configButton(int paramId, std::string name = "")
	{
		return configSwitch<TSwitchQuantity>(paramId, 0.f, 1.f, 0.f, std::move(name));
	}

	ParamQuantity* getParamQuantity(int paramId)
	{
		if (paramId < 0 || (std::size_t)paramId >= paramQuantities.size())
			return nullptr;
		return paramQuantities[paramId];
	}

	void configBypass(int, int) {}                     // MOCK

	// Declared virtual because modules override them with `override`. MOCK
	// throughout: GMPI persists a plugin's state itself, so nothing here reads
	// or writes JSON — but Random and SequentialSwitch override fromJson() to
	// migrate pre-2.0 patches, and that has to compile and chain to a base.
	virtual void paramsToJson(json_t*) {}      // MOCK
	virtual void paramsFromJson(json_t*) {}    // MOCK
	virtual json_t* toJson() { return nullptr; }        // MOCK
	virtual void fromJson(json_t*) {}                   // MOCK

	struct SampleRateChangeEvent { float sampleRate{ 44100.f }; float sampleTime{ 1.f / 44100.f }; };

	// Rack pairs every lifecycle hook with an event struct and keeps the legacy
	// no-argument form working by forwarding. WTVCO and WTLFO override the
	// event forms, so both spellings have to exist and stay connected.
	struct AddEvent {};
	struct RemoveEvent {};
	struct SaveEvent {};
	struct BypassEvent {};
	struct UnBypassEvent {};

	virtual void onRandomize() {}
	virtual void onRandomize(const RandomizeEvent&) { onRandomize(); }
	virtual void onSampleRateChange() {}
	virtual void onSampleRateChange(const SampleRateChangeEvent&) { onSampleRateChange(); }
	virtual void onAdd() {}
	virtual void onAdd(const AddEvent&) { onAdd(); }
	virtual void onRemove() {}
	virtual void onRemove(const RemoveEvent&) { onRemove(); }
	virtual void onSave(const SaveEvent&) {}
	virtual void onBypass(const BypassEvent&) {}
	virtual void onUnBypass(const UnBypassEvent&) {}

	// Rack gives each module instance a folder inside the .vcv patch to stash
	// large binary state — WTVCO and WTLFO write their loaded wavetable there.
	//
	// MOCK: there is no patch bundle to write into, so this reports "no
	// directory" and the modules skip persisting. Combined with the stubbed
	// file dialog (compat/osdialog.h), nothing has a wavetable to save anyway.
	std::string getPatchStorageDirectory() { return {}; }       // MOCK
	std::string createPatchStorageDirectory() { return {}; }    // MOCK

	PortInfo* getInputInfo(int portId)
	{ return (portId >= 0 && (std::size_t)portId < inputInfos.size()) ? inputInfos[portId] : nullptr; }

	PortInfo* getOutputInfo(int portId)
	{ return (portId >= 0 && (std::size_t)portId < outputInfos.size()) ? outputInfos[portId] : nullptr; }

	template<class TLightInfo = LightInfo>
	TLightInfo* configLight(int lightId, std::string name = "")
	{
		if (lightId < 0 || lightId >= (int)lightInfos.size())
			return nullptr;

		delete lightInfos[lightId];
		auto* info = new TLightInfo;
		lightInfos[lightId] = info;

		info->module = this;
		info->lightId = lightId;
		info->name = std::move(name);
		return info;
	}

	// Rack has both forms and the event one dispatches to the legacy no-arg
	// version, so a module may override either. Keep the forwarding: Unity
	// overrides onReset(), Fade overrides onReset(const ResetEvent&).
	virtual void onReset() {}
	virtual void onReset(const ResetEvent&) { onReset(); }

	virtual void process(const ProcessArgs&) {}

	virtual json_t* dataToJson() { return nullptr; }
	virtual void    dataFromJson(json_t*) {}
};

// ---------------------------------------------------------------------------
// nanovg — MOCK, and the honest limit of this adaptor.
//
// A module with a CUSTOM WIDGET draws it by issuing nanovg calls in its
// widget's draw(const DrawArgs&). Supporting that would mean implementing
// nanovg on top of the GMPI drawing API — a project in itself. These stubs
// exist so such a module still COMPILES and its DSP still runs; its custom
// display simply does not appear. Scopes, meters and graph displays are the
// modules affected.
// ---------------------------------------------------------------------------
struct NVGcolor { float r{}, g{}, b{}, a{ 1.f }; };
struct NVGpaint {};

inline NVGcolor nvgRGB(int r, int g, int b)
{ return { r / 255.f, g / 255.f, b / 255.f, 1.f }; }

inline NVGcolor nvgRGBA(int r, int g, int b, int a)
{ return { r / 255.f, g / 255.f, b / 255.f, a / 255.f }; }

inline NVGcolor nvgRGBAf(float r, float g, float b, float a) { return { r, g, b, a }; }
inline NVGcolor nvgRGBf(float r, float g, float b)           { return { r, g, b, 1.f }; }

inline NVGcolor nvgTransRGBA(NVGcolor c, int a) { c.a = a / 255.f; return c; }
inline NVGcolor nvgLerpRGBA(NVGcolor a, NVGcolor, float)     { return a; }

// The drawing backend a module's own draw() code runs on.
//
// A module that draws part of its panel — Scope's trace, VCA-1's VU meter,
// ADSR's envelope curve — writes nanovg calls. This is the interface those
// calls land on, and RackEditor.h implements it over gmpi_ui.
//
// WHY AN ABSTRACT INTERFACE rather than gmpi_ui directly: rack.hpp must not
// know about gmpi_ui. A DSP-only build (RACK_ADAPTOR_NO_GUI) compiles this
// header, and compiles the module's widget code along with it, without gmpi_ui
// on the include path at all. With no backend attached every call below is a
// no-op — which is exactly right for a build with no GUI.
//
// It is also what keeps the mock honest: nothing here pretends to draw.
struct NanoVgBackend
{
	virtual ~NanoVgBackend() = default;

	// Paths are accumulated, then painted by fill() or stroke().
	virtual void beginPath() {}
	virtual void closePath() {}
	virtual void moveTo(float, float) {}
	virtual void lineTo(float, float) {}
	virtual void bezierTo(float, float, float, float, float, float) {}
	virtual void rect(float, float, float, float) {}
	virtual void roundedRect(float, float, float, float, float) {}
	virtual void circle(float, float, float) {}
	virtual void ellipse(float, float, float, float) {}
	virtual void fill() {}
	virtual void stroke() {}

	virtual void fillColor(NVGcolor) {}
	virtual void strokeColor(NVGcolor) {}
	virtual void strokeWidth(float) {}
	virtual void globalAlpha(float) {}

	virtual void save() {}
	virtual void restore() {}
	virtual void scissor(float, float, float, float) {}
	virtual void resetScissor() {}
	virtual void translate(float, float) {}
	virtual void scale(float, float) {}
	virtual void rotate(float) {}

	virtual void fontSize(float) {}
	virtual void textAlign(int) {}
	virtual void textLetterSpacing(float) {}
	virtual float text(float, float, const char*, const char*) { return 0.f; }
};

// What a module's draw() calls receive as `args.vg`. Null backend = no-op.
struct NVGcontext { NanoVgBackend* backend{}; };

#define RACK_NVG_FORWARD(call) do { if (vg && vg->backend) vg->backend->call; } while (false)

inline void nvgBeginPath(NVGcontext* vg) { RACK_NVG_FORWARD(beginPath()); }
inline void nvgClosePath(NVGcontext* vg) { RACK_NVG_FORWARD(closePath()); }
inline void nvgMoveTo(NVGcontext* vg, float x, float y) { RACK_NVG_FORWARD(moveTo(x, y)); }
inline void nvgLineTo(NVGcontext* vg, float x, float y) { RACK_NVG_FORWARD(lineTo(x, y)); }
inline void nvgBezierTo(NVGcontext* vg, float c1x, float c1y, float c2x, float c2y, float x, float y)
{ RACK_NVG_FORWARD(bezierTo(c1x, c1y, c2x, c2y, x, y)); }
inline void nvgRect(NVGcontext* vg, float x, float y, float w, float h) { RACK_NVG_FORWARD(rect(x, y, w, h)); }
inline void nvgRoundedRect(NVGcontext* vg, float x, float y, float w, float h, float r)
{ RACK_NVG_FORWARD(roundedRect(x, y, w, h, r)); }
inline void nvgCircle(NVGcontext* vg, float cx, float cy, float r) { RACK_NVG_FORWARD(circle(cx, cy, r)); }
inline void nvgEllipse(NVGcontext* vg, float cx, float cy, float rx, float ry) { RACK_NVG_FORWARD(ellipse(cx, cy, rx, ry)); }
inline void nvgFill(NVGcontext* vg) { RACK_NVG_FORWARD(fill()); }
inline void nvgStroke(NVGcontext* vg) { RACK_NVG_FORWARD(stroke()); }
inline void nvgFillColor(NVGcontext* vg, NVGcolor c) { RACK_NVG_FORWARD(fillColor(c)); }
inline void nvgStrokeColor(NVGcontext* vg, NVGcolor c) { RACK_NVG_FORWARD(strokeColor(c)); }
inline void nvgStrokeWidth(NVGcontext* vg, float w) { RACK_NVG_FORWARD(strokeWidth(w)); }
inline void nvgGlobalAlpha(NVGcontext* vg, float a) { RACK_NVG_FORWARD(globalAlpha(a)); }
inline void nvgSave(NVGcontext* vg) { RACK_NVG_FORWARD(save()); }
inline void nvgRestore(NVGcontext* vg) { RACK_NVG_FORWARD(restore()); }
inline void nvgScissor(NVGcontext* vg, float x, float y, float w, float h) { RACK_NVG_FORWARD(scissor(x, y, w, h)); }
inline void nvgIntersectScissor(NVGcontext* vg, float x, float y, float w, float h) { RACK_NVG_FORWARD(scissor(x, y, w, h)); }
inline void nvgResetScissor(NVGcontext* vg) { RACK_NVG_FORWARD(resetScissor()); }
inline void nvgTranslate(NVGcontext* vg, float x, float y) { RACK_NVG_FORWARD(translate(x, y)); }
inline void nvgScale(NVGcontext* vg, float x, float y) { RACK_NVG_FORWARD(scale(x, y)); }
inline void nvgRotate(NVGcontext* vg, float a) { RACK_NVG_FORWARD(rotate(a)); }
inline void nvgFontSize(NVGcontext* vg, float s) { RACK_NVG_FORWARD(fontSize(s)); }
inline void nvgTextAlign(NVGcontext* vg, int a) { RACK_NVG_FORWARD(textAlign(a)); }
inline void nvgTextLetterSpacing(NVGcontext* vg, float s) { RACK_NVG_FORWARD(textLetterSpacing(s)); }

inline float nvgText(NVGcontext* vg, float x, float y, const char* str, const char* end)
{
	return (vg && vg->backend) ? vg->backend->text(x, y, str, end) : 0.f;
}

// MOCK, and each for a reason:
//   fontFaceId    — the backend resolves its own font; Rack's ShareTechMono is
//                   not shipped, so a face id would name nothing.
//   lineCap/Join/miterLimit — joins are the renderer's business here.
//   composite ops — layer 1 IS the additive pass, so NVG_LIGHTER is implied.
//   paints        — no module in this set fills with a gradient.
inline void nvgFontFaceId(NVGcontext*, int) {}
inline void nvgLineCap(NVGcontext*, int) {}
inline void nvgLineJoin(NVGcontext*, int) {}
inline void nvgMiterLimit(NVGcontext*, float) {}
inline void nvgGlobalCompositeOperation(NVGcontext*, int) {}
inline void nvgGlobalCompositeBlendFunc(NVGcontext*, int, int) {}
inline void nvgFillPaint(NVGcontext*, NVGpaint) {}
inline void nvgTextBox(NVGcontext*, float, float, float, const char*, const char*) {}
inline NVGpaint nvgLinearGradient(NVGcontext*, float, float, float, float, NVGcolor, NVGcolor) { return {}; }
inline NVGpaint nvgRadialGradient(NVGcontext*, float, float, float, float, NVGcolor, NVGcolor) { return {}; }

enum NVGlineCap { NVG_BUTT, NVG_ROUND, NVG_SQUARE, NVG_BEVEL, NVG_MITER };
enum NVGalign
{
	NVG_ALIGN_LEFT = 1<<0, NVG_ALIGN_CENTER = 1<<1, NVG_ALIGN_RIGHT = 1<<2,
	NVG_ALIGN_TOP = 1<<3, NVG_ALIGN_MIDDLE = 1<<4, NVG_ALIGN_BOTTOM = 1<<5,
	NVG_ALIGN_BASELINE = 1<<6,
};
enum NVGcompositeOperation { NVG_SOURCE_OVER, NVG_SOURCE_IN, NVG_LIGHTER, NVG_ATOP, NVG_COPY };

static const NVGcolor SCHEME_BLACK        { 0.f, 0.f, 0.f, 1.f };
static const NVGcolor SCHEME_WHITE        { 1.f, 1.f, 1.f, 1.f };
static const NVGcolor SCHEME_RED          { 0.929f, 0.196f, 0.216f, 1.f };
static const NVGcolor SCHEME_ORANGE       { 1.f, 0.6f, 0.f, 1.f };
static const NVGcolor SCHEME_YELLOW       { 1.f, 0.797f, 0.f, 1.f };
static const NVGcolor SCHEME_GREEN        { 0.f, 0.789f, 0.f, 1.f };
static const NVGcolor SCHEME_LIGHT_GREEN  { 0.541f, 0.788f, 0.15f, 1.f };
static const NVGcolor SCHEME_CYAN         { 0.f, 0.791f, 0.797f, 1.f };
static const NVGcolor SCHEME_BLUE         { 0.f, 0.6f, 1.f, 1.f };
static const NVGcolor SCHEME_PURPLE       { 0.541f, 0.286f, 0.596f, 1.f };
static const NVGcolor SCHEME_LIGHT_GRAY   { 0.855f, 0.855f, 0.855f, 1.f };
static const NVGcolor SCHEME_DARK_GRAY    { 0.125f, 0.125f, 0.125f, 1.f };

// Deferred from ParamQuantity, which is declared before Module.
inline float ParamQuantity::getValue() const
{
	if (!module || paramId < 0 || (std::size_t)paramId >= module->params.size())
		return defaultValue;

	return module->params[paramId].value;
}

inline void ParamQuantity::setValue(float v)
{
	if (!module || paramId < 0 || (std::size_t)paramId >= module->params.size())
		return;

	module->params[paramId].value = math::clamp(v, minValue, maxValue);
}

namespace color {
	static const NVGcolor BLACK_TRANSPARENT{ 0.f, 0.f, 0.f, 0.f };
	static const NVGcolor BLACK{ 0.f, 0.f, 0.f, 1.f };
	static const NVGcolor WHITE{ 1.f, 1.f, 1.f, 1.f };
	static const NVGcolor RED  { 1.f, 0.f, 0.f, 1.f };
	static const NVGcolor GREEN{ 0.f, 1.f, 0.f, 1.f };
	static const NVGcolor BLUE { 0.f, 0.f, 1.f, 1.f };

	inline NVGcolor alpha(NVGcolor c, float a) { c.a *= a; return c; }
	inline NVGcolor mult(NVGcolor c, float k)  { c.r *= k; c.g *= k; c.b *= k; return c; }
	inline NVGcolor lerp(NVGcolor a, NVGcolor, float) { return a; }
}

// Rack's helper for splatting a Vec into an x, y argument pair.
#define VEC_ARGS(v) (v).x, (v).y
#define RECT_ARGS(r) (r).pos.x, (r).pos.y, (r).size.x, (r).size.y

// ---------------------------------------------------------------------------
// widgets — all MOCK. They build a tree that is constructed and then dropped.
// Panel dimensions in Rack are 1px = 1/75in; a HP is 5.08mm.
// ---------------------------------------------------------------------------
static constexpr float RACK_GRID_WIDTH  = 15.0f;
static constexpr float RACK_GRID_HEIGHT = 380.0f;

// SynthEdit's rack row is 384 DIPs (it prefers multiples of 12; TIDE's E5
// ruling), so a VCV panel is 4 DIPs SHORTER than the row it sits in. A ported
// module therefore reports the FULL row height and draws its 380-tall panel
// centred in it — otherwise every VCV module rides high in the row with a gap
// under it. The same offset must reach EVERYTHING positioned in panel
// coordinates, or they shear apart by 2 DIPs: the drawn art, the editor's
// pointer hit-testing, and the generated <PatchPoint> centres — SynthEdit
// hit-tests cables against those, not against anything the editor draws.
static constexpr float HOST_ROW_HEIGHT       = 384.0f;
static constexpr float PANEL_CENTRE_OFFSET_Y = (HOST_ROW_HEIGHT - RACK_GRID_HEIGHT) / 2.0f;

inline float mm2px(float mm)  { return mm * (75.0f / 25.4f); }
inline Vec   mm2px(Vec v)     { return { mm2px(v.x), mm2px(v.y) }; }

struct Widget;

// Rack's event types. A custom widget overrides onButton, onDragHover and so
// on with `override`, so the signatures have to match exactly — but nothing
// here ever DISPATCHES one. The adaptor's editor drives its own hit-testing
// (see RackEditor.h); these exist so the module's code compiles, and so the
// handlers are already in place for the day the editor forwards to them.
namespace event {

struct Base
{
	mutable Widget* target{};
	mutable bool isConsumed{};

	void consume(Widget* w) const { target = w; isConsumed = true; }
	void stopPropagating() const { isConsumed = true; }
	bool isPropagating() const { return !isConsumed; }
	void unconsume() const { target = nullptr; isConsumed = false; }
};

struct PositionBase { Vec pos; };
struct KeyBase      { int key{}, scancode{}, action{}, mods{}; };
struct TextBase     { int codepoint{}; };

struct Hover        : Base, PositionBase { Vec mouseDelta; };
struct Button       : Base, PositionBase { int button{}, action{}, mods{}; };
struct DoubleClick  : Base {};
struct HoverKey     : Base, PositionBase, KeyBase {};
struct HoverText    : Base, PositionBase, TextBase {};
struct HoverScroll  : Base, PositionBase { Vec scrollDelta; };
struct Enter        : Base {};
struct Leave        : Base {};
struct Select       : Base {};
struct Deselect     : Base {};
struct SelectKey    : Base, KeyBase {};
struct SelectText   : Base, TextBase {};

struct DragBase     : Base { int button{}; };
struct DragStart    : DragBase {};
struct DragEnd      : DragBase {};
struct DragMove     : DragBase { Vec mouseDelta; };
struct DragHover    : DragBase, PositionBase { Widget* origin{}; Vec mouseDelta; };
struct DragEnter    : DragBase { Widget* origin{}; };
struct DragLeave    : DragBase { Widget* origin{}; };
struct DragDrop     : DragBase { Widget* origin{}; };

struct PathDrop     : Base, PositionBase { std::vector<std::string> paths; };
struct Action       : Base {};
struct Change       : Base {};
struct Dirty        : Base {};
struct Reposition   : Base {};
struct Resize       : Base {};
struct Add          : Base {};
struct Remove       : Base {};
struct Show         : Base {};
struct Hide         : Base {};
struct ContextCreate  : Base { NVGcontext* vg{}; };
struct ContextDestroy : Base { NVGcontext* vg{}; };

} // namespace event

struct Widget
{
	// What a custom widget's draw() receives. `vg` is always null here — see
	// the nanovg note above.
	struct DrawArgs
	{
		NVGcontext* vg{};
		Rect clipBox;
		void* fb{};
	};

	Rect box;
	Widget* parent{};
	std::vector<Widget*> children;
	bool visible{ true };

	virtual ~Widget() = default;
	void addChild(Widget* w) { if (w) w->parent = this; children.push_back(w); }
	void addChildBottom(Widget* w) { addChild(w); }
	void addChildBelow(Widget* w, Widget*) { addChild(w); }
	void removeChild(Widget* w)
	{
		children.erase(std::remove(children.begin(), children.end(), w), children.end());
		if (w) w->parent = nullptr;
	}
	void clearChildren() { for (auto* c : children) if (c) c->parent = nullptr; children.clear(); }

	Rect getBox() const { return box; }
	void setBox(Rect b) { box = b; }
	Vec  getSize() const { return box.size; }
	void setSize(Vec v) { box.size = v; }
	Vec  getPosition() const { return box.pos; }
	void setPosition(Vec v) { box.pos = v; }
	float getWidth() const { return box.size.x; }
	float getHeight() const { return box.size.y; }
	bool isVisible() const { return visible; }
	void setVisible(bool v) { visible = v; }
	void show() { visible = true; }
	void hide() { visible = false; }

	// Walk up to the enclosing ModuleWidget and the like. Needs `parent`, which
	// is why addChild now sets it.
	template<class T> T* getAncestorOfType()
	{
		for (Widget* w = parent; w; w = w->parent)
			if (auto* t = dynamic_cast<T*>(w))
				return t;

		return nullptr;
	}

	template<class T> T* getFirstDescendantOfType()
	{
		for (auto* c : children)
		{
			if (!c)
				continue;

			if (auto* t = dynamic_cast<T*>(c))
				return t;

			if (auto* t = c->getFirstDescendantOfType<T>())
				return t;
		}
		return nullptr;
	}

	// Rack's Widget::draw recurses into the children, translating by each
	// child's box.pos. Custom widgets chain to it — DigitalDisplay ends its
	// own drawLayer with Widget::drawLayer(args, layer) — so the recursion has
	// to be here, not in whatever is driving the top-level walk.
	virtual void draw(const DrawArgs& args)
	{
		drawChildren(args, [](Widget* w, const DrawArgs& a) { w->draw(a); });
	}

	virtual void drawLayer(const DrawArgs& args, int layer)
	{
		drawChildren(args, [layer](Widget* w, const DrawArgs& a) { w->drawLayer(a, layer); });
	}

	virtual void step() {}

	template<class F>
	void drawChildren(const DrawArgs& args, F&& each)
	{
		for (auto* child : children)
		{
			if (!child || !child->visible)
				continue;

			nvgSave(args.vg);
			nvgTranslate(args.vg, child->box.pos.x, child->box.pos.y);
			each(child, args);
			nvgRestore(args.vg);
		}
	}

	// Rack names each event type twice and modules use both spellings.
	using BaseEvent        = event::Base;
	using HoverEvent       = event::Hover;
	using ButtonEvent      = event::Button;
	using DoubleClickEvent = event::DoubleClick;
	using HoverKeyEvent    = event::HoverKey;
	using HoverTextEvent   = event::HoverText;
	using HoverScrollEvent = event::HoverScroll;
	using EnterEvent       = event::Enter;
	using LeaveEvent       = event::Leave;
	using SelectEvent      = event::Select;
	using DeselectEvent    = event::Deselect;
	using SelectKeyEvent   = event::SelectKey;
	using SelectTextEvent  = event::SelectText;
	using DragStartEvent   = event::DragStart;
	using DragEndEvent     = event::DragEnd;
	using DragMoveEvent    = event::DragMove;
	using DragHoverEvent   = event::DragHover;
	using DragEnterEvent   = event::DragEnter;
	using DragLeaveEvent   = event::DragLeave;
	using DragDropEvent    = event::DragDrop;
	using PathDropEvent    = event::PathDrop;
	using ActionEvent      = event::Action;
	using ChangeEvent      = event::Change;
	using DirtyEvent       = event::Dirty;
	using RepositionEvent  = event::Reposition;
	using ResizeEvent      = event::Resize;
	using AddEvent         = event::Add;
	using RemoveEvent      = event::Remove;
	using ShowEvent        = event::Show;
	using HideEvent        = event::Hide;
	using ContextCreateEvent  = event::ContextCreate;
	using ContextDestroyEvent = event::ContextDestroy;

	virtual void onHover(const HoverEvent&) {}
	virtual void onButton(const ButtonEvent&) {}
	virtual void onDoubleClick(const DoubleClickEvent&) {}
	virtual void onHoverKey(const HoverKeyEvent&) {}
	virtual void onHoverText(const HoverTextEvent&) {}
	virtual void onHoverScroll(const HoverScrollEvent&) {}
	virtual void onEnter(const EnterEvent&) {}
	virtual void onLeave(const LeaveEvent&) {}
	virtual void onSelect(const SelectEvent&) {}
	virtual void onDeselect(const DeselectEvent&) {}
	virtual void onSelectKey(const SelectKeyEvent&) {}
	virtual void onSelectText(const SelectTextEvent&) {}
	virtual void onDragStart(const DragStartEvent&) {}
	virtual void onDragEnd(const DragEndEvent&) {}
	virtual void onDragMove(const DragMoveEvent&) {}
	virtual void onDragHover(const DragHoverEvent&) {}
	virtual void onDragEnter(const DragEnterEvent&) {}
	virtual void onDragLeave(const DragLeaveEvent&) {}
	virtual void onDragDrop(const DragDropEvent&) {}
	virtual void onPathDrop(const PathDropEvent&) {}
	virtual void onAction(const ActionEvent&) {}
	virtual void onChange(const ChangeEvent&) {}
	virtual void onDirty(const DirtyEvent&) {}
	virtual void onReposition(const RepositionEvent&) {}
	virtual void onResize(const ResizeEvent&) {}
	virtual void onAdd(const AddEvent&) {}
	virtual void onRemove(const RemoveEvent&) {}
	virtual void onShow(const ShowEvent&) {}
	virtual void onHide(const HideEvent&) {}
	virtual void onContextCreate(const ContextCreateEvent&) {}
	virtual void onContextDestroy(const ContextDestroyEvent&) {}
};

// Rack's widget flavours differ only in event routing and framebuffer caching,
// neither of which is modelled — so each is Widget under a different name.
struct OpaqueWidget      : Widget {};
struct TransparentWidget : Widget {};
struct FramebufferWidget : Widget { bool dirty{ true }; float oversample{ 1.f }; };
struct ZoomWidget        : Widget { float zoom{ 1.f }; };

// Panel art, keyed by the path the module names in createPanel(). A plugin's
// build stages its res/*.svg into here (see the generated panel-resources
// header), so nothing has to hand the art to the editor by hand.
//
// Lookup is deliberately LAZY: createPanel() records the path and the editor
// resolves it at construction, long after static init. Resolving eagerly
// would make this race the panel registrations across translation units,
// where inline-variable initialization order is unspecified.
class PanelRegistry
{
public:
	static PanelRegistry& instance()
	{
		static PanelRegistry singleton;
		return singleton;
	}

	// Returns bool so a plugin can register with an inline variable.
	bool add(std::string path, const char* svg)
	{
		panels_.push_back({ std::move(path), svg });
		return true;
	}

	const char* find(std::string_view path) const
	{
		for (const auto& p : panels_)
		{
			if (p.path == path)
				return p.svg;
		}
		return nullptr;
	}

private:
	struct Entry { std::string path; const char* svg; };
	std::vector<Entry> panels_;
};

// The dark rounded panel a module's display sits on. REAL: Rack draws this in
// LedDisplay::draw, and the panel SVG does not include it — leave it out and
// Scope's trace floats on bare panel.
struct LedDisplay : Widget
{
	void draw(const DrawArgs& args) override
	{
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.0f);
		nvgFillColor(args.vg, nvgRGB(0x19, 0x19, 0x19));
		nvgFill(args.vg);

		Widget::draw(args);
	}
};
struct LedDisplaySeparator : Widget {};
struct LedDisplayChoice : Widget { std::string text; std::string fontPath; Vec textOffset; NVGcolor color{}; NVGcolor bgColor{}; };
struct LedDisplayTextField : Widget { std::string text; std::string fontPath; Vec textOffset; NVGcolor color{}; };

struct SvgPanel : Widget
{
	std::string path;   // resolved against PanelRegistry when it is needed
};
struct ParamWidget : Widget
{
	Module* module{};
	int paramId{};

	// Does this control turn? The editor draws a pointer line on the ones that
	// do, since Fundamental's panel art has the knob cap but not the pointer.
	// A button, latch or switch gets none — Scope's 1x2 and TRIG are latches,
	// and a pointer on them reads as a tiny broken knob.
	//
	// Default false so a control type nobody has classified yet is left plain
	// rather than decorated with a pointer that means nothing.
	bool isKnob{ false };

	// What the knob looks like, as sRGB hex.
	//
	// Fundamental's panel SVGs carry NO knob artwork — VCA's has not a single
	// circle or ellipse in it — because Rack composites a component SVG on top
	// at runtime. So the editor draws the body as well as the pointer, and
	// these say what to draw. A black cap with a white pointer is Rack's
	// RoundBlackKnob family; Trimpot overrides them for its silver cap.
	uint32_t knobRimHex{ 0x0D0D0D };
	uint32_t knobBodyHex{ 0x262626 };
	uint32_t knobPointerHex{ 0xF5F5F5 };

	// Modules reach through this for the parameter's range and current value.
	ParamQuantity* getParamQuantity() const
	{
		if (!module || paramId < 0 || (std::size_t)paramId >= module->paramQuantities.size())
			return nullptr;

		return module->paramQuantities[paramId];
	}
};
struct PortWidget  : Widget { Module* module{}; int portId{}; };

// The concrete Fundamental control set.
//
// SIZES ARE REAL and load-bearing: generatePluginXml() turns a port widget's
// size into its patch-point hit radius. Rack takes these from each component's
// SVG, which the mock has no reader for, so the panel dimensions are stated
// here instead. A PJ301M is an 8.7mm panel jack; the knobs are sized for
// completeness and nothing reads them yet.

// REAL, and read back: the editor draws these. `firstLightId` says which of
// the module's lights this widget shows, and `baseColors` says in what colour
// — one entry per consecutive light from firstLightId, which is how a
// bi-colour light (Rack's GreenRedLight, Fundamental's YellowBlueLight) works.
// The module drives the brightnesses; the widget only says how to paint them.
struct LightWidget : Widget
{
	Module* module{};
	int firstLightId{ -1 };

	std::vector<NVGcolor> baseColors;
	void addBaseColor(NVGcolor c) { baseColors.push_back(c); }

	// The unlit lens and its outline. Fundamental's own light classes assign
	// these (VCVBezelLightBig clears both), so they have to be here.
	NVGcolor color{};
	NVGcolor bgColor{};
	NVGcolor borderColor{};

	// Rack's "Simple" light variants are the same lamp without the glow. The
	// distinction is load-bearing on a lit BUTTON, where a halo the size of
	// the button reads as the whole control lighting up.
	bool hasHalo{ true };
};

// Rack's default appearance for a panel light: a half-grey lens at a third
// opacity, faintly outlined. Fundamental's plugin.hpp derives its lights from
// this and calls addBaseColor() per colour.
struct GrayModuleLightWidget : LightWidget
{
	GrayModuleLightWidget()
	{
		bgColor     = { 0.5f, 0.5f, 0.5f, 0.33f };
		borderColor = { 0.0f, 0.0f, 0.0f, 0.15f };
	}
};

// Vector art. MOCK: the adaptor draws panels through SvgParser and component
// art procedurally, so a loaded Svg is a handle to nothing.
struct Svg
{
	std::string path;
	static std::shared_ptr<Svg> load(const std::string& p)
	{
		auto s = std::make_shared<Svg>();
		s->path = p;
		return s;
	}
};

struct Font { int handle{}; };

struct SvgWidget : Widget
{
	std::shared_ptr<Svg> svg;
	void setSvg(std::shared_ptr<Svg> s) { svg = s; if (s) box.size = Vec(0.f, 0.f); }
	void wrap() {}
};

namespace window {
struct Window
{
	std::shared_ptr<Font> loadFont(const std::string&) { return nullptr; }
	std::shared_ptr<Svg>  loadSvg(const std::string& p) { return Svg::load(p); }
};
}

// Rack's global app context. MOCK — enough for a module to ask for a font and
// get nothing back, which the display code already handles.
// Scope asks the rack which cable is plugged into its X and Y inputs, so it
// can draw each trace in that cable's colour. MOCK: there is no rack and no
// cable, so it always answers "nothing patched" and Scope falls back to its
// default colours — which is exactly what it does in Rack for a bare input.
struct CableWidget : Widget
{
	NVGcolor color{};
	PortWidget* inputPort{};
	PortWidget* outputPort{};
};

struct RackWidget : Widget
{
	CableWidget* getTopCable(PortWidget*) const { return nullptr; }   // MOCK
	std::vector<CableWidget*> getCompleteCablesOnPort(PortWidget*) const { return {}; }
};

struct Scene : Widget
{
	RackWidget* rack{};
	Scene() : rack(new RackWidget) {}
};

struct AppContext
{
	window::Window* window{ nullptr };
	Scene* scene{ nullptr };
	AppContext() : window(new window::Window), scene(new Scene) {}
};

inline AppContext* appContext()
{
	static AppContext ctx;
	return &ctx;
}

#define APP (::rack::appContext())

namespace app {

// A switch built from SVG frames.
struct SvgSwitch : ParamWidget
{
	bool momentary{ false };
	std::vector<std::shared_ptr<Svg>> frames;
	void addFrame(std::shared_ptr<Svg> f) { frames.push_back(std::move(f)); }
};

struct SvgKnob : ParamWidget { float minAngle{}, maxAngle{}; };
struct SvgPort : PortWidget {};
struct SvgScrew : Widget {};

using LightWidget = ::rack::LightWidget;
using ModuleLightWidget = ::rack::LightWidget;

} // namespace app


// Rack composes these as MediumLight<RedLight>: the size class wraps the
// colour class. Each states its colour(s) the same way a module's own light
// class does — one base colour per consecutive light id — so the editor needs
// no table of its own.
struct RedLight    : GrayModuleLightWidget { RedLight()    { addBaseColor(SCHEME_RED);    } };
struct YellowLight : GrayModuleLightWidget { YellowLight() { addBaseColor(SCHEME_YELLOW); } };
struct GreenLight  : GrayModuleLightWidget { GreenLight()  { addBaseColor(SCHEME_GREEN);  } };
struct BlueLight   : GrayModuleLightWidget { BlueLight()   { addBaseColor(SCHEME_BLUE);   } };
struct WhiteLight  : GrayModuleLightWidget { WhiteLight()  { addBaseColor(SCHEME_WHITE);  } };

struct GreenRedLight : GrayModuleLightWidget
{
	GreenRedLight() { addBaseColor(SCHEME_GREEN); addBaseColor(SCHEME_RED); }
};

struct RedGreenBlueLight : GrayModuleLightWidget
{
	RedGreenBlueLight() { addBaseColor(SCHEME_RED); addBaseColor(SCHEME_GREEN); addBaseColor(SCHEME_BLUE); }
};

// NOTE: YellowBlueLight, YellowRedLight, VCVBezelBig, VCVBezelLightBig,
// DigitalDisplay, ChannelDisplay and createRangeItem are NOT defined here.
// Fundamental declares them in its own src/plugin.hpp, which is module source
// and is copied in alongside the modules. Defining them here would shadow the
// real ones and quietly diverge.

// What a lit control's lamp looks like, taken from its TLight argument by
// constructing one and reading it. Rack gets there by making the light an
// actual CHILD widget of the button; the effect on what gets painted is the
// same — as long as the light's own SIZE comes across too.
//
// That size is the whole point. A VCVLightLatch is 9mm across and the
// MediumSimpleLight inside it is 3.176mm. Paint the lamp at the button's size
// and the entire bezel lights up instead of the LED in the middle of it.
struct LightAppearance
{
	std::vector<NVGcolor> colors;
	Vec  size{};
	bool hasHalo{ true };
};

template<typename TLight>
inline LightAppearance lightAppearanceOf()
{
	TLight probe;
	return { probe.baseColors, probe.box.size, probe.hasHalo };
}

// Size classes wrap a colour class. The "Simple" variants differ only in
// having no halo in Rack, which is not modelled.
template<typename TBase> struct TinyLight   : TBase { TinyLight()   { this->box.size = mm2px(Vec(1.088f, 1.088f)); } };
template<typename TBase> struct SmallLight  : TBase { SmallLight()  { this->box.size = mm2px(Vec(2.176f, 2.176f)); } };
template<typename TBase> struct MediumLight : TBase { MediumLight() { this->box.size = mm2px(Vec(3.176f, 3.176f)); } };
template<typename TBase> struct LargeLight  : TBase { LargeLight()  { this->box.size = mm2px(Vec(5.179f, 5.179f)); } };

template<typename TBase> struct TinySimpleLight   : TinyLight<TBase>   { TinySimpleLight()   { this->hasHalo = false; } };
template<typename TBase> struct SmallSimpleLight  : SmallLight<TBase>  { SmallSimpleLight()  { this->hasHalo = false; } };
template<typename TBase> struct MediumSimpleLight : MediumLight<TBase> { MediumSimpleLight() { this->hasHalo = false; } };
template<typename TBase> struct LargeSimpleLight  : LargeLight<TBase>  { LargeSimpleLight()  { this->hasHalo = false; } };

// Controls that are a parameter AND a light in one — a lit button, bezel or
// slider. They carry both ids.
struct LightParamWidget : ParamWidget
{
	int firstLightId{ -1 };

	// Rack builds a child light widget inside the button; this records what
	// that widget would have looked like, so the editor can paint the lit part
	// without a widget tree. Filled by the lit-control templates below from
	// their TLight argument — see lightAppearanceOf().
	//
	// `lightSize` is the LAMP's size, which is much smaller than the button's.
	std::vector<NVGcolor> lightColors;
	Vec  lightSize{};
	bool lightHasHalo{ true };

	void setLightAppearance(const LightAppearance& a)
	{
		lightColors  = a.colors;
		lightSize    = a.size;
		lightHasHalo = a.hasHalo;
	}
};

struct VCVButton   : ParamWidget { VCVButton()   { box.size = mm2px(Vec(9.0f, 9.0f)); } };

// VCA-1's VU meter is a SliderKnob subclass that draws its own bar graph.
struct SliderKnob  : ParamWidget { SliderKnob()   { box.size = mm2px(Vec(5.0f, 30.0f)); } };
struct VCVSlider   : SliderKnob  {};

// The lit controls are TEMPLATES on their light type, and used that way:
// VCVLightBezel<WhiteLight>, VCVLightButton<MediumSimpleLight<WhiteLight>>,
// and sometimes VCVLightBezel<> — hence the default argument.
// Rack's real signature is LightButton<TBase, TLight> deriving TBase, and
// Fundamental uses both arities: VCVLightBezel<WhiteLight> (one) and
// LightButton<VCVBezelBig, VCVBezelLightBig<WhiteLight>> (two). Deriving TBase
// is what makes the two-argument form work, since TBase carries the size and
// the ParamWidget lineage.
struct LightButtonBase : ParamWidget { LightButtonBase() { box.size = mm2px(Vec(9.0f, 9.0f)); } };

template<typename TBase = LightButtonBase, typename TLight = WhiteLight>
struct LightButton : TBase
{
	int firstLightId{ -1 };
	std::vector<NVGcolor> lightColors{ lightAppearanceOf<TLight>().colors };
	Vec  lightSize{ lightAppearanceOf<TLight>().size };
	bool lightHasHalo{ lightAppearanceOf<TLight>().hasHalo };
};

template<typename TLight = WhiteLight>
struct VCVLightButton : LightParamWidget { VCVLightButton() { box.size = mm2px(Vec(9.0f, 9.0f)); setLightAppearance(lightAppearanceOf<TLight>()); } };

template<typename TLight = WhiteLight>
struct VCVLightLatch : LightParamWidget { VCVLightLatch() { box.size = mm2px(Vec(9.0f, 9.0f)); setLightAppearance(lightAppearanceOf<TLight>()); } };

template<typename TLight = WhiteLight>
struct VCVLightBezel : LightParamWidget { VCVLightBezel() { box.size = mm2px(Vec(9.0f, 9.0f)); setLightAppearance(lightAppearanceOf<TLight>()); } };

template<typename TLight = WhiteLight>
struct VCVLightSlider : LightParamWidget { VCVLightSlider() { box.size = mm2px(Vec(5.0f, 30.0f)); setLightAppearance(lightAppearanceOf<TLight>()); } };

struct ThemedScrew      : Widget {};
struct RoundBlackKnob   : ParamWidget { RoundBlackKnob()   { box.size = mm2px(Vec(9.5f, 9.5f)); isKnob = true; } };
struct Trimpot          : ParamWidget { Trimpot()          { box.size = mm2px(Vec(7.0f, 7.0f)); isKnob = true; knobRimHex = 0x8A8A8A; knobBodyHex = 0xC8C8C8; knobPointerHex = 0x1A1A1A; } };
struct CKSS               : ParamWidget { CKSS()               { box.size = mm2px(Vec(3.5f, 7.0f)); } };
struct CKSSThreeHorizontal: ParamWidget { CKSSThreeHorizontal(){ box.size = mm2px(Vec(9.5f, 3.5f)); } };
struct RoundSmallBlackKnob: ParamWidget { RoundSmallBlackKnob(){ box.size = mm2px(Vec(7.0f,  7.0f)); isKnob = true; } };
struct RoundLargeBlackKnob: ParamWidget { RoundLargeBlackKnob(){ box.size = mm2px(Vec(11.0f, 11.0f)); isKnob = true; } };
struct RoundHugeBlackKnob : ParamWidget { RoundHugeBlackKnob() { box.size = mm2px(Vec(14.0f, 14.0f)); isKnob = true; } };
struct ScrewSilver        : Widget      { ScrewSilver()        { box.size = mm2px(Vec(3.0f,  3.0f));  } };

template<typename TLight = WhiteLight>
struct LEDLightBezel : LightParamWidget { LEDLightBezel() { box.size = mm2px(Vec(9.0f, 9.0f)); setLightAppearance(lightAppearanceOf<TLight>()); } };
struct CKSSThree        : ParamWidget { CKSSThree()        { box.size = mm2px(Vec(3.5f, 9.5f)); } };

// 23.7px is the literal size of Rack's res/ComponentLibrary/PJ301M.svg, so
// the jack the editor draws is the size the real artwork would have been.
struct ThemedPJ301MPort : PortWidget { ThemedPJ301MPort() { box.size = Vec(23.7f, 23.7f); } };
struct PJ301MPort       : PortWidget { PJ301MPort()       { box.size = Vec(23.7f, 23.7f); } };
struct PJ3410Port       : PortWidget { PJ3410Port()       { box.size = Vec(31.6f, 31.6f); } };
struct PJNMPort         : PortWidget { PJNMPort()         { box.size = Vec(23.7f, 23.7f); } };
struct ThemedPJNMPort   : PortWidget { ThemedPJNMPort()   { box.size = Vec(23.7f, 23.7f); } };
struct CL1362Port       : PortWidget { CL1362Port()       { box.size = Vec(33.0f, 33.0f); } };

// box.pos holds the widget's TOP-LEFT, exactly as Rack does.
//
// This mock used to store a CENTRE here, because a centre is what patch points,
// jack drawing and knob hit-testing all want and nothing else read box.pos.
// That stopped being true once modules started drawing themselves: a widget's
// own draw() is written against Rack's convention, so it landed half a widget
// off. VCA-1 showed it twice over — its VU meter is a ParamWidget that draws
// its own bars, and it resizes itself AFTER createParam, so a centre computed
// at construction was derived from the wrong size anyway.
//
// The centre is derived where it is needed, in readPanelLayout, from
// box.pos + box.size / 2 — arithmetic on two values that are both final by
// then, so it cannot go stale.
template<class T> T* createWidget(Vec pos)
{
	T* w = new T;
	w->box.pos = pos;
	return w;
}

template<class T> T* createParamCentered(Vec centre, Module* module, int paramId)
{
	T* w = new T;
	w->box.pos = { centre.x - w->box.size.x * 0.5f, centre.y - w->box.size.y * 0.5f };
	w->module = module;
	w->paramId = paramId;
	return w;
}

template<class T> T* createInputCentered(Vec centre, Module* module, int portId)
{
	T* w = new T;
	w->box.pos = { centre.x - w->box.size.x * 0.5f, centre.y - w->box.size.y * 0.5f };
	w->module = module;
	w->portId = portId;
	return w;
}

template<class T> T* createOutputCentered(Vec centre, Module* module, int portId)
{
	T* w = new T;
	w->box.pos = { centre.x - w->box.size.x * 0.5f, centre.y - w->box.size.y * 0.5f };
	w->module = module;
	w->portId = portId;
	return w;
}

// The non-centred forms. Rack takes a top-left here, and so do we.

template<class T> T* createParam(Vec topLeft, Module* module, int paramId)
{
	T* w = new T;
	w->box.pos = topLeft;
	w->module = module;
	w->paramId = paramId;
	return w;
}

template<class T> T* createInput(Vec topLeft, Module* module, int portId)
{
	T* w = new T;
	w->box.pos = topLeft;
	w->module = module;
	w->portId = portId;
	return w;
}

template<class T> T* createOutput(Vec topLeft, Module* module, int portId)
{
	T* w = new T;
	w->box.pos = topLeft;
	w->module = module;
	w->portId = portId;
	return w;
}

template<class T> T* createLight(Vec topLeft, Module* module, int firstLightId)
{
	T* w = new T;
	w->box.pos = topLeft;
	w->module = module;
	w->firstLightId = firstLightId;
	return w;
}

// A control that is both a parameter and a light.
template<class T> T* createLightParamCentered(Vec centre, Module* module, int paramId, int firstLightId)
{
	T* w = new T;
	w->box.pos = { centre.x - w->box.size.x * 0.5f, centre.y - w->box.size.y * 0.5f };
	w->module = module;
	w->paramId = paramId;
	w->firstLightId = firstLightId;
	return w;
}

template<class T> T* createLightParam(Vec topLeft, Module* module, int paramId, int firstLightId)
{
	T* w = new T;
	w->box.pos = topLeft;
	w->module = module;
	w->paramId = paramId;
	w->firstLightId = firstLightId;
	return w;
}

template<class T> T* createLightCentered(Vec centre, Module* module, int firstLightId)
{
	T* w = new T;
	w->box.pos = { centre.x - w->box.size.x * 0.5f, centre.y - w->box.size.y * 0.5f };
	w->module = module;
	w->firstLightId = firstLightId;
	return w;
}

// The light panel's path is kept — that is how the editor finds the art
// without anyone naming it twice. The dark variant is ignored for now.
inline SvgPanel* createPanel(std::string lightSvg, std::string /*darkSvg*/ = "")
{
	auto* p = new SvgPanel;
	p->path = std::move(lightSvg);
	return p;
}

// ---------------------------------------------------------------------------
// menus
//
// REAL, in the sense that matters: a module declares its context menu in
// appendContextMenu(), and this records what it declared so the adaptor can
// turn each entry into a GMPI parameter and a right-click menu.
//
// The recorded `target` is a pointer into the module instance that built the
// menu. That is deliberate and is what makes this work across the editor/DSP
// split: the editor and the processor each own a SEPARATE module instance, so
// each calls appendContextMenu() on its own and binds its own pointer. Nothing
// is shared between them but the parameter value.
// ---------------------------------------------------------------------------
struct MenuItem : Widget
{
	std::string name;
	std::string rightText;   // Rack shows this right-aligned; kept for tooltips etc.

	// An "index pointer" item: a submenu of labels selecting an int by index.
	std::vector<std::string> labels;
	int* target{};           // into the module instance that declared it

	// A "bool pointer" item: a single entry that ticks on and off.
	bool* boolTarget{};

	// The same two shapes again, but stated as accessors rather than a pointer
	// — createIndexSubmenuItem/createBoolMenuItem take lambdas, and Fundamental
	// uses those wherever setting the value has to do more than store it
	// (Merge resizes its channel count, Quantizer flips a bit in a mask).
	//
	// The lambdas capture the module that built the menu, so they carry the
	// same "bound to one instance" property the raw pointers do.
	std::function<int()>      getIndexFn;
	std::function<void(int)>  setIndexFn;
	std::function<bool()>     getBoolFn;
	std::function<void(bool)> setBoolFn;

	bool isSeparator{};

	bool isIndexPtr() const { return (target != nullptr || getIndexFn) && !labels.empty(); }
	bool isBoolPtr()  const { return boolTarget != nullptr || (bool)getBoolFn; }

	// One accessor each, so callers do not care which shape the module used.
	int  readIndex() const { return getIndexFn ? getIndexFn() : (target ? *target : 0); }
	bool readBool()  const { return getBoolFn  ? getBoolFn()  : (boolTarget ? *boolTarget : false); }
};

struct MenuSeparator : MenuItem
{
	MenuSeparator() { isSeparator = true; }
};

struct Menu : Widget
{
	std::vector<MenuItem*> items;

	void addChild(Widget* w)
	{
		if (auto* item = dynamic_cast<MenuItem*>(w))
			items.push_back(item);

		Widget::addChild(w);
	}
};

// Rack's signature takes T* so it works for enums as well as int. Only int-
// sized targets are supported here; anything else is recorded without a
// target and shows as an inert entry rather than silently writing garbage.
// Plain action item, and a range picker. MOCK: recorded but not offered, since
// neither maps onto a parameter the way the ptr-based items do.
inline MenuItem* createMenuItem(std::string text, std::string rightText = "",
                                std::function<void()> action = {}, bool = false, bool = false)
{
	auto* item = new MenuItem;
	item->name = std::move(text);
	item->rightText = std::move(rightText);
	(void)action;
	return item;
}

inline MenuItem* createMenuLabel(std::string text)
{
	auto* item = new MenuItem;
	item->name = std::move(text);
	return item;
}

template<typename T>
MenuItem* createRangeItem(std::string text, T*, T = T(), T = T())
{
	auto* item = new MenuItem;
	item->name = std::move(text);
	return item;   // MOCK
}

template<typename T>
MenuItem* createIndexPtrSubmenuItem(std::string name, std::vector<std::string> labels, T* ptr)
{
	auto* item = new MenuItem;
	item->name = std::move(name);
	item->labels = std::move(labels);

	if constexpr (sizeof(T) == sizeof(int) && std::is_trivially_copyable_v<T>)
		item->target = reinterpret_cast<int*>(ptr);

	return item;
}

// The lambda-based forms of the two above.
inline MenuItem* createIndexSubmenuItem(std::string name, std::vector<std::string> labels,
                                        std::function<int()> getIndex,
                                        std::function<void(int)> setIndex)
{
	auto* item = new MenuItem;
	item->name = std::move(name);
	item->labels = std::move(labels);
	item->getIndexFn = std::move(getIndex);
	item->setIndexFn = std::move(setIndex);
	return item;
}

inline MenuItem* createBoolMenuItem(std::string name, std::string rightText,
                                    std::function<bool()> getBool,
                                    std::function<void(bool)> setBool)
{
	auto* item = new MenuItem;
	item->name = std::move(name);
	item->rightText = std::move(rightText);
	item->getBoolFn = std::move(getBool);
	item->setBoolFn = std::move(setBool);
	return item;
}

// A tick that runs an action rather than holding a value, and a submenu that
// builds its children on demand. MOCK: neither maps onto a parameter, so both
// are recorded as inert entries and the adaptor skips them.
inline MenuItem* createCheckMenuItem(std::string text, std::string rightText,
                                     std::function<bool()> = {}, std::function<void()> = {},
                                     bool = false, bool = true)
{
	auto* item = new MenuItem;
	item->name = std::move(text);
	item->rightText = std::move(rightText);
	return item;
}

inline MenuItem* createSubmenuItem(std::string text, std::string rightText,
                                   std::function<void(Menu*)> = {}, bool = false)
{
	auto* item = new MenuItem;
	item->name = std::move(text);
	item->rightText = std::move(rightText);
	return item;
}

// A single on/off entry. Widely used across Fundamental — Mixer, Rescale,
// SEQ3, SequentialSwitch, Unity, VCMixer — always with a bool member.
template<typename T>
MenuItem* createBoolPtrMenuItem(std::string name, std::string rightText, T* ptr)
{
	auto* item = new MenuItem;
	item->name = std::move(name);
	item->rightText = std::move(rightText);

	if constexpr (std::is_same_v<T, bool>)
		item->boolTarget = ptr;

	return item;
}

// ---------------------------------------------------------------------------
// module widget
// ---------------------------------------------------------------------------
struct ModuleWidget : Widget
{
	Module* module{};

	// Kept, not just parented: this is where the module states each jack's
	// position and which port it belongs to, and generatePluginXml() reads it
	// back to place the patch points. Rack exposes the same thing through
	// getInput()/getOutput().
	std::vector<ParamWidget*> paramWidgets;
	std::vector<PortWidget*>  inputWidgets;
	std::vector<PortWidget*>  outputWidgets;

	// Whatever the module passed to setPanel(createPanel("res/Foo.svg")).
	std::string panelPath;

	void setModule(Module* m) { module = m; }

	void setPanel(Widget* p)
	{
		if (auto* svg = dynamic_cast<SvgPanel*>(p))
			panelPath = svg->path;

		addChild(p);
	}

	void addParam(ParamWidget* w) { paramWidgets.push_back(w);  addChild(w); }
	void addInput(PortWidget* w)  { inputWidgets.push_back(w);  addChild(w); }
	void addOutput(PortWidget* w) { outputWidgets.push_back(w); addChild(w); }

	template<class T> T* getModule() { return dynamic_cast<T*>(module); }

	PortWidget* getInput(int portId) const
	{
		for (auto* w : inputWidgets)
			if (w && w->portId == portId)
				return w;

		return nullptr;
	}

	PortWidget* getOutput(int portId) const
	{
		for (auto* w : outputWidgets)
			if (w && w->portId == portId)
				return w;

		return nullptr;
	}

	virtual void appendContextMenu(Menu*) {}
};

// ---------------------------------------------------------------------------
// plugin / model registration
// ---------------------------------------------------------------------------
struct Plugin {};

// NOT defined here: Fundamental's own plugin.hpp declares
//     extern Plugin* pluginInstance;
// at global scope, so defining it inside namespace rack as well makes every
// use ambiguous. The definition is at the bottom of this file, at global
// scope, where that extern can find it.

namespace asset {
inline std::string plugin(Plugin*, std::string filename) { return filename; }   // MOCK
inline std::string system(std::string filename) { return filename; }            // MOCK
}

struct Model
{
	std::string slug;

	virtual ~Model() = default;
	virtual Module*       createModule() { return nullptr; }
	virtual ModuleWidget* createModuleWidget(Module*) { return nullptr; }
};

namespace engine {
using Module        = ::rack::Module;
using Param         = ::rack::Param;
using Port          = ::rack::Port;
using Input         = ::rack::Input;
using Output        = ::rack::Output;
using Light         = ::rack::Light;
using ParamQuantity = ::rack::ParamQuantity;
using PortInfo      = ::rack::Module::PortInfo;
using LightInfo     = ::rack::Module::LightInfo;
}

namespace widget {
using Widget            = ::rack::Widget;
using OpaqueWidget      = ::rack::OpaqueWidget;
using TransparentWidget = ::rack::TransparentWidget;
using FramebufferWidget = ::rack::FramebufferWidget;
using ZoomWidget        = ::rack::ZoomWidget;
using SvgWidget         = ::rack::SvgWidget;
}

namespace ui {
using Menu = ::rack::Menu;
using MenuItem = ::rack::MenuItem;
using MenuSeparator = ::rack::MenuSeparator;
}

} // namespace rack

// Declared, not defined: RackAutoRegister.h supplies the body, and createModel
// below calls it so that a module registers itself with GMPI simply by being
// compiled. The call is dependent on createModel's template arguments, so it
// is looked up when createModel is instantiated — by which point the
// definition is visible, because the module's .cpp is #included AFTER
// RackModule.h. Include only plugin.hpp and you get a link error naming this,
// which is the intended signpost.
//
// Define RACK_NO_AUTO_REGISTER before the module's .cpp to opt out and call
// rack_adaptor::registerModule() yourself.
namespace rack_adaptor
{
template<class TModule, class TModuleWidget>
void autoRegisterModel(rack::Model& model);
}

namespace rack {

// Every Model that createModel() builds, findable by slug.
//
// Rack registers models in the plugin's own init(): `p->addModel(modelFade)`.
// We have no init() — but upstream's registration line, e.g.
//
//     Model* modelFade = createModel<Fade, FadeWidget>("Fade");
//
// is a namespace-scope variable whose initializer runs at static-init time,
// which is the same hook GMPI uses with `auto r = Register<T>::withXml(...)`
// (there too the value is discarded; the initializer's side effect is the
// point). So createModel() adds each Model here on the way past, and the
// adaptor can then reach a module by slug without naming its C++ type.
class ModelRegistry
{
public:
	// Function-local static, NOT a namespace-scope global. The registrations
	// happen during static init, and a global registry might not be
	// constructed yet when the first one runs — it would then be overwritten
	// by its own constructor afterwards, dropping whatever had registered.
	// Constructing on first use removes the ordering question entirely, which
	// is why SynthEdit reaches its factory through ModuleFactory() rather than
	// a bare global too.
	static ModelRegistry& instance()
	{
		static ModelRegistry singleton;
		return singleton;
	}

	void add(Model* model) { models_.push_back(model); }

	Model* find(std::string_view slug) const
	{
		for (auto* m : models_)
		{
			if (m->slug == slug)
				return m;
		}
		return nullptr;
	}

	const std::vector<Model*>& all() const { return models_; }

private:
	std::vector<Model*> models_;
};

// Mirrors Rack's own createModel, and the mirroring matters: the overrides
// below are what odr-use TModule's and TModuleWidget's constructors. Without
// them the compiler parses the module but emits nothing, and a mock missing
// something those constructors need would still "build" — the false pass this
// layer must never give.
template<class TModule, class TModuleWidget>
Model* createModel(std::string slug)
{
	struct TModel : Model
	{
		Module* createModule() override { return new TModule; }

		ModuleWidget* createModuleWidget(Module* m) override
		{
			return new TModuleWidget(m ? dynamic_cast<TModule*>(m) : nullptr);
		}
	};

	TModel* model = new TModel;   // owned by the registry, never freed.
	model->slug = std::move(slug);

	ModelRegistry::instance().add(model);

#ifndef RACK_NO_AUTO_REGISTER
	// This is the whole point: compiling a Rack module's .cpp is all it takes
	// to get a GMPI plugin out of it. No per-module registration to write, and
	// none to forget.
	rack_adaptor::autoRegisterModel<TModule, TModuleWidget>(*model);
#endif

	return model;
}

} // namespace rack

// Rack's real rack.hpp does exactly this, which is why module sources refer to
// Module, Vec, simd::float_4 etc. unqualified.
using namespace rack;

// Satisfies the `extern Plugin* pluginInstance;` a plugin's own project header
// declares. Nothing reads it — asset::plugin() ignores it — but it has to link.
inline rack::Plugin* pluginInstance = nullptr;

// Fundamental DECLARES this in its src/plugin.hpp and defines it in
// src/plugin.cpp, which is Fundamental's own plumbing rather than module
// source, so it is not among the files a port copies in. SHASR calls it.
//
// MOCK: a two-slider gain/offset submenu in Rack; an inert entry here, and one
// the adaptor skips because it maps onto no parameter. Declared at global
// scope to match Fundamental's declaration exactly — the template of the same
// name inside namespace rack is a different overload set.
inline rack::MenuItem* createRangeItem(std::string label, float*, float*)
{
	auto* item = new rack::MenuItem;
	item->name = std::move(label);
	return item;
}
