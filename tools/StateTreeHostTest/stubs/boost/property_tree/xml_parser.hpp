#pragma once
// a small XML reader with boost::property_tree::read_xml's layout: attributes under <xmlattr>, text as data

#include "ptree.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace boost { namespace property_tree { namespace xml_parser {

    constexpr int trim_whitespace = 1;
    constexpr int no_comments = 2;

    class xml_parser_error : public std::runtime_error
    {
    public:
        xml_parser_error(const std::string& what, int atLine) : std::runtime_error(what), lineNumber(atLine) {}
        int line() const { return lineNumber; }
        std::string message() const { return what(); }
    private:
        int lineNumber;
    };

    class Reader
    {
    public:
        explicit Reader(const std::string& text) : text(text) {}

        void Document(ptree& root)
        {
            while (true)
            {
                SkipSpace();
                if (pos >= text.size())
                    return;
                if (Starts("<?"))
                    Skip("?>");
                else if (Starts("<!--"))
                    Skip("-->");
                else if (Starts("<"))
                    Element(root);
                else
                    Fail("text outside the root element");
            }
        }

    private:
        void Element(ptree& parent)
        {
            ++pos;
            const std::string name = Name();
            ptree& node = parent.add_child_raw(name);
            while (true)
            {
                SkipSpace();
                if (Starts("/>"))
                {
                    pos += 2;
                    return;
                }
                if (Starts(">"))
                {
                    ++pos;
                    break;
                }
                const std::string attribute = Name();
                SkipSpace();
                Expect('=');
                SkipSpace();
                const char quote = text[pos];
                if (quote != '"' && quote != '\'')
                    Fail("quote expected");
                const size_t end = text.find(quote, pos + 1);
                if (end == std::string::npos)
                    Fail("unterminated attribute");
                node.put(path_type("<xmlattr>." + attribute), Decode(text.substr(pos + 1, end - pos - 1)));
                pos = end + 1;
            }

            std::string data;
            while (true)
            {
                if (pos >= text.size())
                    Fail("unterminated <" + name + ">");
                if (Starts("</"))
                {
                    pos += 2;
                    const std::string closing = Name();
                    if (closing != name)
                        Fail("</" + closing + "> closes <" + name + ">");
                    SkipSpace();
                    Expect('>');
                    break;
                }
                if (Starts("<!--"))
                    Skip("-->");
                else if (Starts("<"))
                    Element(node);
                else
                {
                    const size_t end = text.find('<', pos);
                    data += Decode(text.substr(pos, end - pos));
                    pos = end;
                }
            }
            const size_t first = data.find_first_not_of(" \t\r\n");
            node.data() = first == std::string::npos ? "" : data.substr(first, data.find_last_not_of(" \t\r\n") - first + 1);
        }

        std::string Name()
        {
            const size_t start = pos;
            while (pos < text.size() && (std::isalnum((unsigned char)text[pos]) || text[pos] == '_' || text[pos] == '-' || text[pos] == ':' || text[pos] == '.'))
                ++pos;
            if (pos == start)
                Fail("name expected");
            return text.substr(start, pos - start);
        }

        static std::string Decode(const std::string& raw)
        {
            std::string out;
            for (size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] != '&')
                {
                    out += raw[i];
                    continue;
                }
                const size_t end = raw.find(';', i);
                const std::string entity = raw.substr(i + 1, end - i - 1);
                out += entity == "lt" ? "<" : entity == "gt" ? ">" : entity == "amp" ? "&" : entity == "quot" ? "\"" : entity == "apos" ? "'" : "?";
                i = end;
            }
            return out;
        }

        bool Starts(const char* prefix) const { return text.compare(pos, std::char_traits<char>::length(prefix), prefix) == 0; }
        void Skip(const char* until)
        {
            const size_t end = text.find(until, pos);
            if (end == std::string::npos)
                Fail(std::string("missing ") + until);
            pos = end + std::char_traits<char>::length(until);
        }
        void SkipSpace() { while (pos < text.size() && std::isspace((unsigned char)text[pos])) ++pos; }
        void Expect(char c)
        {
            if (pos >= text.size() || text[pos] != c)
                Fail(std::string("'") + c + "' expected");
            ++pos;
        }
        [[noreturn]] void Fail(const std::string& why) const
        {
            int line = 1;
            for (size_t i = 0; i < pos && i < text.size(); ++i)
                line += text[i] == '\n';
            throw xml_parser_error(why, line);
        }

        const std::string& text;
        size_t pos = 0;
    };

    inline void read_xml(const std::string& file, ptree& tree, int flags = 0)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            throw xml_parser_error("cannot open file", 0);
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        Reader(text).Document(tree);
    }
}

using xml_parser::read_xml;

}}

namespace boost { namespace property_tree {
    using xml_parser::xml_parser_error;
}}
