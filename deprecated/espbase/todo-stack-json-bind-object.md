### Adding `bind_object`

Repeating the parent path for every single nested key gets exhausting. We can add a `bind_object` feature that lets you pass a flat list of `key, target` pairs, and the compiler will automatically apply the parent path to all of them.

To achieve this without changing the core parsing engine, we need to teach `json_parser` how to "flatten" `std::tuple`s of bindings at compile time.

#### 1. Update `json_parser` for Tuple Flattening

Add this metaprogramming utility above your `json_parser` definition. It recursively unwraps nested tuples into a single flat list.

```cpp
#include <tuple>
#include <type_traits>

namespace detail {
    template <typename T>
    struct is_tuple : std::false_type {};
    
    template <typename... Ts>
    struct is_tuple<std::tuple<Ts...>> : std::true_type {};

    template <typename T>
    auto flatten_impl(T&& t) {
        using Decayed = std::decay_t<T>;
        if constexpr (is_tuple<Decayed>::value) {
            return std::apply([](auto&&... args) {
                // Recursively flatten nested tuples
                return std::tuple_cat(flatten_impl(std::forward<decltype(args)>(args))...);
            }, std::forward<T>(t));
        } else {
            return std::make_tuple(std::forward<T>(t));
        }
    }
}

template <typename... Args>
auto flatten_bindings(Args&&... args) {
    return std::tuple_cat(detail::flatten_impl(std::forward<Args>(args))...);
}

```

Now, update `json_parser` so it flattens its arguments before forwarding them into the `StackJsonParser` constructor:

```cpp
template <typename... Args>
auto json_parser(Args&&... args) {
    auto flat_nodes = flatten_bindings(std::forward<Args>(args)...);
    
    return std::apply([](auto&&... nodes) {
        return StackJsonParser<std::decay_t<decltype(nodes)>...>(
            std::forward<decltype(nodes)>(nodes)...
        );
    }, std::move(flat_nodes));
}

```

#### 2. Implement `bind_object`

This function takes a parent path (or string) and a variadic list of arguments. It iterates over them in pairs (`key`, `target`), prepends the parent path, and returns a tuple of standard `BindNode`s!

```cpp
namespace detail {
    template <typename PathT, typename Tuple, std::size_t... Is>
    auto bind_object_pairs(const PathT& parent_path, Tuple&& tup, std::index_sequence<Is...>) {
        return std::make_tuple(
            // Multiply Is by 2 to grab the key, and 2*Is + 1 for the target
            bind(
                parent_path(std::get<2 * Is>(tup)), 
                std::forward<decltype(std::get<2 * Is + 1>(tup))>(std::get<2 * Is + 1>(tup))
            )...
        );
    }
}

template <typename KeyOrPath, typename... Args>
auto bind_object(const KeyOrPath& parent, Args&&... args) {
    static_assert(sizeof...(Args) % 2 == 0, "bind_object requires key-target pairs (e.g., \"key\", val)");
    
    auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
    
    // C++20 check: if 'parent' is already a path object (can be called with a string), use it.
    // Otherwise, wrap it in path(parent).
    if constexpr (requires { parent("test"); }) {
        return detail::bind_object_pairs(parent, std::move(tup), std::make_index_sequence<sizeof...(Args) / 2>{});
    } else {
        return detail::bind_object_pairs(path(parent), std::move(tup), std::make_index_sequence<sizeof...(Args) / 2>{});
    }
}

```

#### 3. The Test

Because of the tuple flattening, you can seamlessly mix `bind_object` and `bind` inside the same parser, and the `was_set` indices match the exact order they appear in the code!

```cpp
TEST(ParserTest, BindObject) {
    std::string_view json = R"({
        "dongley": {
            "version": "eaf2d7e",
            "image": "dongley.bin"
        },
        "status": "online"
    })";
    
    std::string version, image;
    std::string_view status;
    
    auto parser = json_parser(
        bind_object("dongley",
            "version", version, // index 0
            "image", image      // index 1
        ),
        bind("status", status)  // index 2
    );
    
    parser.parse(json);
    
    EXPECT_TRUE(parser.was_set<0>());
    EXPECT_EQ(version, "eaf2d7e");
    
    EXPECT_TRUE(parser.was_set<1>());
    EXPECT_EQ(image, "dongley.bin");
    
    EXPECT_TRUE(parser.was_set<2>());
    EXPECT_EQ(status, "online");
}

```