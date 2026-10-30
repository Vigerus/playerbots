#pragma once
// the part of boost::property_tree the state tree sources use, std only

#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace boost { namespace property_tree {

    class ptree;

    struct path_type
    {
        std::string path;
        char separator = '.';
        path_type(const std::string& text, char sep = '.') : path(text), separator(sep) {}
        path_type(const char* text) : path(text) {}
    };

    // a child of the optional kind boost returns by reference
    struct optional_child
    {
        const ptree* child = nullptr;
        explicit operator bool() const { return child != nullptr; }
        const ptree& operator*() const { return *child; }
        const ptree* operator->() const { return child; }
    };

    class ptree
    {
    public:
        using value_type = std::pair<const std::string, ptree>;
        using path_type = boost::property_tree::path_type;
        using iterator = std::list<value_type>::iterator;
        using const_iterator = std::list<value_type>::const_iterator;

        ptree() = default;
        ptree(const ptree& other) : value(other.value), children(other.children) {}
        ptree& operator=(const ptree& other)
        {
            if (this != &other)
            {
                value = other.value;
                children.clear();
                for (const value_type& child : other.children)
                    children.emplace_back(child.first, child.second);
            }
            return *this;
        }

        const std::string& data() const { return value; }
        std::string& data() { return value; }

        iterator begin() { return children.begin(); }
        iterator end() { return children.end(); }
        const_iterator begin() const { return children.begin(); }
        const_iterator end() const { return children.end(); }

        ptree& add_child_raw(const std::string& key) { children.emplace_back(key, ptree()); return children.back().second; }

        const ptree* find(const path_type& path) const
        {
            const ptree* node = this;
            size_t start = 0;
            while (node)
            {
                const size_t at = path.separator ? path.path.find(path.separator, start) : std::string::npos;
                const std::string key = path.path.substr(start, at == std::string::npos ? std::string::npos : at - start);
                const ptree* next = nullptr;
                for (const value_type& child : node->children)
                    if (child.first == key)
                    {
                        next = &child.second;
                        break;
                    }
                node = next;
                if (at == std::string::npos)
                    return node;
                start = at + 1;
            }
            return nullptr;
        }

        template<class T>
        T get(const path_type& path, const T& fallback) const
        {
            const ptree* node = find(path);
            return node ? T(node->value) : fallback;
        }

        std::string get(const path_type& path, const char* fallback) const
        {
            const ptree* node = find(path);
            return node ? node->value : std::string(fallback);
        }

        template<class T>
        std::optional<T> get_optional(const path_type& path) const
        {
            const ptree* node = find(path);
            if (!node)
                return std::nullopt;
            return T(node->value);
        }

        optional_child get_child_optional(const path_type& path) const { return { find(path) }; }

        size_t erase(const std::string& key)
        {
            size_t count = 0;
            for (auto it = children.begin(); it != children.end(); )
            {
                if (it->first == key)
                {
                    it = children.erase(it);
                    ++count;
                }
                else
                    ++it;
            }
            return count;
        }

        void put(const path_type& path, const std::string& data)
        {
            ptree* node = this;
            size_t start = 0;
            while (true)
            {
                const size_t at = path.path.find(path.separator, start);
                const std::string key = path.path.substr(start, at == std::string::npos ? std::string::npos : at - start);
                ptree* next = nullptr;
                for (value_type& child : node->children)
                    if (child.first == key)
                    {
                        next = &child.second;
                        break;
                    }
                if (!next)
                    next = &node->add_child_raw(key);
                node = next;
                if (at == std::string::npos)
                    break;
                start = at + 1;
            }
            node->value = data;
        }

    private:
        std::string value;
        std::list<value_type> children;
    };
}}
