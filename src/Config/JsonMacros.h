#pragma once

#include <nlohmann/json.hpp>

#define NLOHMANN_JSON_SERIALIZE_ENUM(ENUM_TYPE, ...)                                                                   \
    template <>                                                                                                        \
    struct nlohmann::adl_serializer<ENUM_TYPE> {                                                                       \
        static void to_json(json& j, const ENUM_TYPE& e) { j = e; }                                                    \
        static void from_json(const json& j, ENUM_TYPE& e) {                                                           \
            if (j.is_string()) {                                                                                       \
                e = nlohmann::json::parse(j.get<std::string>());                                                       \
            } else {                                                                                                   \
                e = j.get<ENUM_TYPE>();                                                                                \
            }                                                                                                          \
        }                                                                                                              \
    };

#define NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Type, ...)                                                     \
    inline void to_json(nlohmann::json& nlohmann_json_j, const Type& nlohmann_json_t) {                                \
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__))                                       \
    }                                                                                                                  \
    inline void from_json(const nlohmann::json& nlohmann_json_j, Type& nlohmann_json_t) {                              \
        const Type nlohmann_json_default_obj;                                                                          \
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__))                        \
    }

#define NLOHMANN_JSON_FROM_WITH_DEFAULT(v1)                                                                            \
    if (nlohmann_json_j.contains(#v1)) {                                                                               \
        nlohmann_json_j.at(#v1).get_to(nlohmann_json_t.v1);                                                            \
    } else {                                                                                                           \
        nlohmann_json_t.v1 = nlohmann_json_default_obj.v1;                                                             \
    }