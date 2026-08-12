// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// Getting a module's DSP state to the thing that draws it.
//
// THE PROBLEM. The editor and the processor own separate module instances and
// only parameter values cross between them. A module that draws part of its own
// panel from what process() computed — Scope's trace, VCA-1's VU meter — draws
// nothing on the editor side, because the editor's copy never runs.
//
// WHY IT CANNOT BE AUTOMATIC. The adaptor has no way to know which members a
// display reads. Copying the whole module is not an option either: a Rack
// Module holds vectors and a vtable, and blindly copying those bytes across
// would corrupt both instances. So a port states the members explicitly, and
// the compiler checks the statement:
//
//     #include "RackModule.h"
//     #include "vcv/Scope.cpp"
//
//     RACK_DISPLAY_STATE(&Scope::pointBuffer, &Scope::channelsX,
//                        &Scope::channelsY,   &Scope::bufferIndex)
//
// That is the entire per-module cost, it lives beside the #include rather than
// in the module, and upstream's file stays byte-for-byte. The module type is
// deduced from the member pointers, so nothing has to name it twice, and every
// member is static_asserted trivially copyable — list a std::string and you get
// a compile error rather than a heap corruption six months later.
//
// WHAT IT COSTS. One blob parameter, sent by the DSP and read by the editor.
// Rate-limited to display rate rather than block rate: this is a picture, and
// Scope's buffer is 64KB. Nothing is sent for a module that declares no state,
// and no pin is generated for one either.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "rack.hpp"

namespace rack_adaptor
{

// Pull the class and the value type back out of a pointer-to-member. Declared
// and never defined — these are only ever used inside decltype.
//
// The results come back wrapped in type_identity because a function may not
// RETURN an array type, and Scope's pointBuffer is Point[256][2][16]. Deducing
// it bare compiles for every scalar member and then fails on the one that
// matters most.
template<class C, class V> std::type_identity<C> memberClassOf(V C::*);
template<class C, class V> std::type_identity<V> memberValueOf(V C::*);

template<auto M> using MemberClass = typename decltype(memberClassOf(M))::type;
template<auto M> using MemberValue = typename decltype(memberValueOf(M))::type;

// How to move one module type's display state across, type-erased so the
// generic processor and editor can use it without naming the type.
struct DisplayStateCodec
{
	std::size_t size{};
	void (*capture)(const rack::Module&, std::uint8_t*){};
	void (*apply)(rack::Module&, const std::uint8_t*){};
};

// Keyed by the module's DYNAMIC type, which is what the processor and editor
// actually have — both hold a rack::Module* they got from a Model, and neither
// knows the concrete type. typeid saves plumbing a slug through three layers.
inline std::unordered_map<std::type_index, DisplayStateCodec>& displayStateRegistry()
{
	static std::unordered_map<std::type_index, DisplayStateCodec> registry;
	return registry;
}

// Behind RACK_DISPLAY_STATE. The members are non-type template arguments, so
// they are compile-time constants and the copy loops below fold into a handful
// of memcpys with no indirection.
template<auto First, auto... Rest>
inline bool registerDisplayState()
{
	using T = MemberClass<First>;

	static_assert((std::is_same_v<MemberClass<Rest>, T> && ...),
		"RACK_DISPLAY_STATE: every member must belong to the same module type.");

	static_assert(std::is_base_of_v<rack::Module, T>,
		"RACK_DISPLAY_STATE: the members must belong to a rack::Module subclass.");

	static_assert(std::is_trivially_copyable_v<MemberValue<First>>
		&& (std::is_trivially_copyable_v<MemberValue<Rest>> && ...),
		"RACK_DISPLAY_STATE: every member must be trivially copyable. A member "
		"that owns memory (std::string, std::vector) cannot be copied as bytes "
		"between two module instances - send what it points AT instead.");

	DisplayStateCodec codec;
	codec.size = sizeof(MemberValue<First>) + (sizeof(MemberValue<Rest>) + ... + 0);

	codec.capture = [](const rack::Module& module, std::uint8_t* out)
	{
		const auto& m = static_cast<const T&>(module);
		std::size_t offset = 0;

		const auto one = [&](auto member)
		{
			const auto& value = m.*member;
			std::memcpy(out + offset, &value, sizeof(value));
			offset += sizeof(value);
		};

		one(First);
		(one(Rest), ...);
	};

	codec.apply = [](rack::Module& module, const std::uint8_t* in)
	{
		auto& m = static_cast<T&>(module);
		std::size_t offset = 0;

		const auto one = [&](auto member)
		{
			auto& value = m.*member;
			std::memcpy(&value, in + offset, sizeof(value));
			offset += sizeof(value);
		};

		one(First);
		(one(Rest), ...);
	};

	displayStateRegistry()[std::type_index(typeid(T))] = codec;
	return true;
}

// What does this module declare, if anything? Null means "nothing to send",
// which is the case for every module whose panel is entirely static.
inline const DisplayStateCodec* findDisplayState(const rack::Module& module)
{
	const auto& registry = displayStateRegistry();
	const auto it = registry.find(std::type_index(typeid(module)));
	return (it == registry.end()) ? nullptr : &it->second;
}

} // namespace rack_adaptor

// Declare which of a module's members its own display code reads.
//
//     RACK_DISPLAY_STATE(&Scope::pointBuffer, &Scope::channelsX)
//
// Place it in the port's .cpp, after the #include of upstream's source. The
// module type is deduced; listing members of two different modules in one
// call is a compile error, as is listing a member that owns memory.
#define RACK_DISPLAY_STATE(...)                                                \
	namespace {                                                                \
	const bool rackDisplayStateRegistered =                                    \
		::rack_adaptor::registerDisplayState<__VA_ARGS__>();                   \
	}
