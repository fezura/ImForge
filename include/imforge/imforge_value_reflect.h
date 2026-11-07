#pragma once

#include "imforge_value.h"

#define IMFORGE_EXPAND(x) x
#define IMFORGE_CAT_(a, b) a##b
#define IMFORGE_CAT(a, b) IMFORGE_CAT_(a, b)
#define IMFORGE_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, \
                      _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, N, \
                      ...) N
#define IMFORGE_RSEQ_N() \
    32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#define IMFORGE_NARG_(...) IMFORGE_EXPAND(IMFORGE_ARG_N(__VA_ARGS__))
#define IMFORGE_NARG(...) IMFORGE_NARG_(__VA_ARGS__, IMFORGE_RSEQ_N())
#define IMFORGE_FE_1(M, x) M(x)
#define IMFORGE_FE_2(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_1(M, __VA_ARGS__))
#define IMFORGE_FE_3(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_2(M, __VA_ARGS__))
#define IMFORGE_FE_4(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_3(M, __VA_ARGS__))
#define IMFORGE_FE_5(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_4(M, __VA_ARGS__))
#define IMFORGE_FE_6(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_5(M, __VA_ARGS__))
#define IMFORGE_FE_7(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_6(M, __VA_ARGS__))
#define IMFORGE_FE_8(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_7(M, __VA_ARGS__))
#define IMFORGE_FE_9(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_8(M, __VA_ARGS__))
#define IMFORGE_FE_10(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_9(M, __VA_ARGS__))
#define IMFORGE_FE_11(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_10(M, __VA_ARGS__))
#define IMFORGE_FE_12(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_11(M, __VA_ARGS__))
#define IMFORGE_FE_13(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_12(M, __VA_ARGS__))
#define IMFORGE_FE_14(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_13(M, __VA_ARGS__))
#define IMFORGE_FE_15(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_14(M, __VA_ARGS__))
#define IMFORGE_FE_16(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_15(M, __VA_ARGS__))
#define IMFORGE_FE_17(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_16(M, __VA_ARGS__))
#define IMFORGE_FE_18(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_17(M, __VA_ARGS__))
#define IMFORGE_FE_19(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_18(M, __VA_ARGS__))
#define IMFORGE_FE_20(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_19(M, __VA_ARGS__))
#define IMFORGE_FE_21(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_20(M, __VA_ARGS__))
#define IMFORGE_FE_22(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_21(M, __VA_ARGS__))
#define IMFORGE_FE_23(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_22(M, __VA_ARGS__))
#define IMFORGE_FE_24(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_23(M, __VA_ARGS__))
#define IMFORGE_FE_25(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_24(M, __VA_ARGS__))
#define IMFORGE_FE_26(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_25(M, __VA_ARGS__))
#define IMFORGE_FE_27(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_26(M, __VA_ARGS__))
#define IMFORGE_FE_28(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_27(M, __VA_ARGS__))
#define IMFORGE_FE_29(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_28(M, __VA_ARGS__))
#define IMFORGE_FE_30(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_29(M, __VA_ARGS__))
#define IMFORGE_FE_31(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_30(M, __VA_ARGS__))
#define IMFORGE_FE_32(M, x, ...) M(x) IMFORGE_EXPAND(IMFORGE_FE_31(M, __VA_ARGS__))
#define IMFORGE_FOREACH(M, ...) \
    IMFORGE_EXPAND(IMFORGE_CAT(IMFORGE_FE_, IMFORGE_NARG(__VA_ARGS__))(M, __VA_ARGS__))

#if defined(__GNUC__)
#define IMFORGE_OFFSETOF_PUSH _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")
#define IMFORGE_OFFSETOF_POP _Pragma("GCC diagnostic pop")
#else
#define IMFORGE_OFFSETOF_PUSH
#define IMFORGE_OFFSETOF_POP
#endif

#define IMFORGE_FIELD_(f) \
    si.fields.push_back(::imforge::FieldInfo{#f, ::imforge::CTypeOf<decltype(ImForgeT::f)>(), offsetof(ImForgeT, f)});
#define IMFORGE_ENUM_VALUE_(v) {#v, static_cast<long long>(ImForgeE::v)},

// Use at global scope (or inside namespace imforge), nested types first. 1..32 fields.
// Uses offsetof, so no polymorphic types or virtual bases. Self-referential types are fine.
#define IMFORGE_REFLECT(Type, ...)                                                            \
    template <> struct imforge::TypeName<Type> {                                              \
        static constexpr const char* value = #Type;                                           \
    };                                                                                        \
    IMFORGE_OFFSETOF_PUSH                                                                     \
    template <> struct imforge::Reflector<Type> {                                             \
        using ImForgeT = Type;                                                                \
        static ::imforge::StructInfo Build() {                                                \
            ::imforge::StructInfo si;                                                         \
            si.name = #Type;                                                                  \
            si.size = sizeof(Type);                                                           \
            IMFORGE_FOREACH(IMFORGE_FIELD_, __VA_ARGS__)                                      \
            return si;                                                                        \
        }                                                                                     \
        static const ::imforge::StructInfo* Placeholder() {                                   \
            static const ::imforge::StructInfo* p = ::imforge::TypeRegistry::Global()         \
                .RegisterStruct(::imforge::StructInfo{#Type, sizeof(Type), {}});              \
            return p;                                                                         \
        }                                                                                     \
        static bool& Building() {                                                             \
            static bool b = false;                                                            \
            return b;                                                                         \
        }                                                                                     \
        static const ::imforge::StructInfo* Fill() {                                          \
            const ::imforge::StructInfo* p = Placeholder();                                   \
            Building() = true;                                                                \
            ::imforge::TypeRegistry::Global().RegisterStruct(Build());                        \
            Building() = false;                                                               \
            return p;                                                                         \
        }                                                                                     \
        static const ::imforge::StructInfo* Info() {                                          \
            static const ::imforge::StructInfo* p = Fill();                                   \
            return p;                                                                         \
        }                                                                                     \
        static const ::imforge::StructInfo* Ref() {                                           \
            return Building() ? Placeholder() : Info();                                       \
        }                                                                                     \
        static inline const ::imforge::StructInfo* const registered = Info();                 \
    };                                                                                        \
    IMFORGE_OFFSETOF_POP                                                                      \
    static_assert(sizeof(Type) > 0, "IMFORGE_REFLECT requires a complete type")

#define IMFORGE_REFLECT_ENUM(Enum, ...)                                                       \
    template <> struct imforge::TypeName<Enum> {                                              \
        static constexpr const char* value = #Enum;                                           \
    };                                                                                        \
    template <> struct imforge::EnumReflector<Enum> {                                         \
        using ImForgeE = Enum;                                                                \
        static ::imforge::EnumInfo Build() {                                                  \
            ::imforge::EnumInfo ei;                                                           \
            ei.name = #Enum;                                                                  \
            ei.values = {IMFORGE_FOREACH(IMFORGE_ENUM_VALUE_, __VA_ARGS__)};                  \
            return ei;                                                                        \
        }                                                                                     \
        static const ::imforge::EnumInfo* Info() {                                            \
            static const ::imforge::EnumInfo* p =                                             \
                ::imforge::TypeRegistry::Global().RegisterEnum(Build());                      \
            return p;                                                                         \
        }                                                                                     \
        static inline const ::imforge::EnumInfo* const registered = Info();                   \
    };                                                                                        \
    static_assert(std::is_enum<Enum>::value, "IMFORGE_REFLECT_ENUM requires an enum")

namespace imforge {

template <class T> inline const StructInfo* StructInfoOf() { return Reflector<T>::Info(); }
template <class T> inline const EnumInfo* EnumInfoOf() { return EnumReflector<T>::Info(); }

}  // namespace imforge
