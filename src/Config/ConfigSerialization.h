#pragma once

#include "config.h"
#include <nlohmann/json.hpp>

#include <boost/pfr.hpp>
#include <string_view>
#include <type_traits>
#include <utility> // for std::index_sequence


namespace nlohmann {

// 通用序列化模板，适用于所有聚合类型 (C++17 或更高版本)
template <typename T>
    requires std::is_aggregate_v<T>
struct adl_serializer<T> {
    /**
     * @brief 将聚合类型 T 序列化为 JSON 对象。
     */
    static void to_json(json& j, const T& value) {
        j = json::object();
        // 获取结构体成员的数量
        constexpr auto field_count = boost::pfr::tuple_size_v<T>;

        // 使用 C++17 的模板 lambda 和折叠表达式在编译时遍历所有成员
        [&]<size_t... I>(std::index_sequence<I...>) {
            // ( expression, ... ) 是折叠表达式的语法
            // 它会为 I 的每一个值（0, 1, 2, ...）展开内部的表达式
            ((j[boost::pfr::get_name<I, T>()] = boost::pfr::get<I>(value)), ...);
        }(std::make_index_sequence<field_count>{});
    }

    /**
     * @brief 从 JSON 对象反序列化为聚合类型 T。
     */
    static void from_json(const json& j, T& value) {
        const T        default_value{};
        constexpr auto field_count = boost::pfr::tuple_size_v<T>;

        // 同样使用编译时展开技术
        [&]<size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 // 获取编译时成员名称
                 constexpr std::string_view name = boost::pfr::get_name<I, T>();

                 if (j.contains(name)) {
                     // 从 JSON 中获取值并赋给对应成员
                     j.at(name).get_to(boost::pfr::get<I>(value));
                 } else {
                     // 从默认构造的对象中获取默认值
                     boost::pfr::get<I>(value) = boost::pfr::get<I>(default_value);
                 }
             }()),
             ...); // 立即调用内部 lambda
        }(std::make_index_sequence<field_count>{});
    }
};

} // namespace nlohmann
