#include "io/npy.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>

#include <miniz.h>

#include "io/temp_file.hpp"

namespace signals::io {
namespace {

using core::Errc;

constexpr std::string_view kMagic{"\x93NUMPY", 6};
constexpr std::size_t kMaxHeader = std::size_t{1} << 20;

std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return {u.begin(), u.end()};
}

std::unexpected<core::Error> fail(std::string message) { return core::make_error(Errc::invalid_argument, std::move(message)); }

std::unexpected<core::Error> at(const std::filesystem::path& p, const core::Error& e) {
    return core::make_error(e.code, utf8(p) + ": " + e.message);
}

struct Dtype {
    char order = '<';
    char kind = 'f';
    std::size_t size = 8;
};

struct Header {
    Dtype dtype;
    bool fortran = false;
    std::vector<std::size_t> shape;
    std::size_t data_offset = 0;
    std::size_t count = 1;
    std::size_t item() const { return dtype.kind == 'U' ? dtype.size * 4 : dtype.size; }
};

class Literal {
public:
    explicit Literal(std::string_view s) : s_{s} {}
    bool eat(char c) {
        skip();
        if (i_ < s_.size() && s_[i_] == c) return ++i_, true;
        return false;
    }
    char peek() {
        skip();
        return i_ < s_.size() ? s_[i_] : '\0';
    }
    std::optional<std::string> string() {
        skip();
        if (i_ >= s_.size() || (s_[i_] != '\'' && s_[i_] != '"')) return std::nullopt;
        const char quote = s_[i_++];
        std::string out;
        while (i_ < s_.size() && s_[i_] != quote) out += s_[i_++];
        if (i_ >= s_.size()) return std::nullopt;
        ++i_;
        return out;
    }
    std::optional<std::string> word() {
        skip();
        std::string out;
        while (i_ < s_.size() && ((s_[i_] >= 'a' && s_[i_] <= 'z') || (s_[i_] >= 'A' && s_[i_] <= 'Z'))) out += s_[i_++];
        if (out.empty()) return std::nullopt;
        return out;
    }
    std::optional<std::size_t> integer() {
        skip();
        std::size_t v = 0;
        bool any = false;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
            const auto d = static_cast<std::size_t>(s_[i_++] - '0');
            if (v > (std::numeric_limits<std::size_t>::max() - d) / 10) return std::nullopt;
            v = v * 10 + d;
            any = true;
        }
        if (i_ < s_.size() && s_[i_] == 'L') ++i_;
        if (!any) return std::nullopt;
        return v;
    }

private:
    void skip() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    std::string_view s_;
    std::size_t i_ = 0;
};

std::string shape_text(const std::vector<std::size_t>& shape) {
    std::string out = "(";
    for (std::size_t k = 0; k < shape.size(); ++k) out += (k ? ", " : "") + std::to_string(shape[k]);
    if (shape.size() == 1) out += ",";
    return out + ")";
}

core::Result<Dtype> parse_dtype(const std::string& s) {
    Dtype d;
    std::size_t i = 0;
    if (!s.empty() && (s[0] == '<' || s[0] == '>' || s[0] == '|' || s[0] == '=')) {
        d.order = s[0] == '=' ? (std::endian::native == std::endian::little ? '<' : '>') : s[0];
        i = 1;
    } else {
        d.order = '|';
    }
    if (i >= s.size()) return fail("npy dtype '" + s + "' is not understood");
    d.kind = s[i++];
    if (d.kind == 'O') return fail("npy object arrays need pickle and are not supported (dtype '" + s + "')");
    if (d.kind == 'c') return fail("complex npy arrays are not supported (dtype '" + s + "')");
    std::size_t size = 0;
    bool any = false;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        size = size * 10 + static_cast<std::size_t>(s[i] - '0');
        any = true;
        if (size > (std::size_t{1} << 20)) return fail("npy dtype '" + s + "' is not supported");
    }
    if (i != s.size() || !any) return fail("npy dtype '" + s + "' is not understood");
    d.size = size;
    const bool ok = (d.kind == 'f' && (size == 4 || size == 8)) || ((d.kind == 'i' || d.kind == 'u') && (size == 1 || size == 2 || size == 4 || size == 8)) ||
                    (d.kind == 'b' && size == 1) || (d.kind == 'U' && size >= 1);
    if (!ok) return fail("npy dtype '" + s + "' is not supported");
    return d;
}

core::Result<Header> parse_dict(std::string_view text) {
    Literal p{text};
    if (!p.eat('{')) return fail("npy header is not a dict");
    std::optional<std::string> descr;
    std::optional<bool> fortran;
    std::optional<std::vector<std::size_t>> shape;
    while (!p.eat('}')) {
        const auto key = p.string();
        if (!key || !p.eat(':')) return fail("npy header is malformed");
        if (*key == "descr") {
            if (p.peek() == '[') return fail("structured npy arrays are not supported");
            descr = p.string();
            if (!descr) return fail("npy header: descr is not a string");
        } else if (*key == "fortran_order") {
            const auto w = p.word();
            if (!w || (*w != "True" && *w != "False")) return fail("npy header: fortran_order is not True or False");
            fortran = *w == "True";
        } else if (*key == "shape") {
            if (!p.eat('(')) return fail("npy header: shape is not a tuple");
            std::vector<std::size_t> dims;
            while (!p.eat(')')) {
                const auto n = p.integer();
                if (!n) return fail("npy header: shape is not a tuple of sizes");
                dims.push_back(*n);
                if (!p.eat(',') && p.peek() != ')') return fail("npy header: shape is malformed");
            }
            shape = std::move(dims);
        } else {
            return fail("npy header has an unknown key '" + *key + "'");
        }
        if (!p.eat(',') && p.peek() != '}') return fail("npy header is malformed");
    }
    if (!descr || !fortran || !shape) return fail("npy header misses descr, fortran_order or shape");
    auto dtype = parse_dtype(*descr);
    if (!dtype) return std::unexpected{dtype.error()};
    Header h;
    h.dtype = *dtype;
    h.fortran = *fortran;
    h.shape = std::move(*shape);
    return h;
}

core::Result<Header> parse_header(std::string_view bytes, const NpLimits& limits) {
    if (bytes.size() < 10 || bytes.substr(0, kMagic.size()) != kMagic) return fail("not an .npy array: no \\x93NUMPY magic");
    const auto byte = [&](std::size_t k) { return static_cast<std::size_t>(static_cast<unsigned char>(bytes[k])); };
    const auto major = byte(6);
    std::size_t length = 0, start = 0;
    if (major == 1) {
        length = byte(8) | byte(9) << 8;
        start = 10;
    } else if (major == 2 || major == 3) {
        if (bytes.size() < 12) return fail("truncated .npy header");
        length = byte(8) | byte(9) << 8 | byte(10) << 16 | byte(11) << 24;
        start = 12;
    } else {
        return fail("unsupported .npy format version " + std::to_string(major));
    }
    if (length > kMaxHeader) return fail(".npy header is too long");
    if (bytes.size() < start + length) return fail("truncated .npy header");
    auto h = parse_dict(bytes.substr(start, length));
    if (!h) return h;
    h->data_offset = start + length;
    const std::size_t row = h->shape.empty() ? 1 : h->shape.back();
    std::size_t rows = 1;
    for (std::size_t k = 0; k + 1 < h->shape.size(); ++k) {
        const auto d = h->shape[k];
        if (d != 0 && rows > limits.rows / d)
            return fail("array shape " + shape_text(h->shape) + " exceeds the limit of " + std::to_string(limits.rows) + " rows");
        rows *= d;
    }
    if (row > limits.row)
        return fail("array shape " + shape_text(h->shape) + " exceeds the limit of " + std::to_string(limits.row) + " values per row");
    h->count = rows * row;
    return h;
}

template <class T>
double as_double(const unsigned char* b) {
    T v;
    std::memcpy(&v, b, sizeof v);
    return static_cast<double>(v);
}

double read_number(const char* p, const Dtype& d, bool swap) {
    std::array<unsigned char, 8> b{};
    const std::size_t n = std::min<std::size_t>(d.size, b.size());
    for (std::size_t k = 0; k < n; ++k) b[k] = static_cast<unsigned char>(p[swap ? n - 1 - k : k]);
    switch (d.kind) {
    case 'f': return d.size == 4 ? as_double<float>(b.data()) : as_double<double>(b.data());
    case 'b': return b[0] != 0 ? 1.0 : 0.0;
    case 'i':
        switch (d.size) {
        case 1: return as_double<std::int8_t>(b.data());
        case 2: return as_double<std::int16_t>(b.data());
        case 4: return as_double<std::int32_t>(b.data());
        default: return as_double<std::int64_t>(b.data());
        }
    default:
        switch (d.size) {
        case 1: return as_double<std::uint8_t>(b.data());
        case 2: return as_double<std::uint16_t>(b.data());
        case 4: return as_double<std::uint32_t>(b.data());
        default: return as_double<std::uint64_t>(b.data());
        }
    }
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string read_text(const char* p, std::size_t chars, bool swap) {
    std::vector<std::uint32_t> cps(chars);
    for (std::size_t k = 0; k < chars; ++k) {
        std::array<unsigned char, 4> b{};
        std::memcpy(b.data(), p + 4 * k, 4);
        if (swap) std::reverse(b.begin(), b.end());
        std::memcpy(&cps[k], b.data(), 4);
    }
    while (!cps.empty() && cps.back() == 0) cps.pop_back();
    std::string out;
    for (const auto cp : cps) append_utf8(out, cp);
    return out;
}

std::optional<std::vector<std::uint32_t>> code_points(std::string_view s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return std::nullopt;
        std::uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < n; ++k) {
            const auto t = static_cast<unsigned char>(s[i + k]);
            if ((t >> 6) != 0x2) return std::nullopt;
            cp = cp << 6 | (t & 0x3F);
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::size_t c_index(std::size_t f, const std::vector<std::size_t>& shape) {
    std::size_t c = 0, stride = 1;
    std::vector<std::size_t> idx(shape.size());
    for (std::size_t k = 0; k < shape.size(); ++k) {
        idx[k] = f % shape[k];
        f /= shape[k];
    }
    for (std::size_t k = shape.size(); k-- > 0;) {
        c += idx[k] * stride;
        stride *= shape[k];
    }
    return c;
}

void put_le(std::string& out, std::uint64_t v, std::size_t bytes) {
    for (std::size_t k = 0; k < bytes; ++k) out += static_cast<char>((v >> (8 * k)) & 0xFF);
}

core::Result<std::string> read_file(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    if (!in) return core::make_error(Errc::io_error, "cannot open " + utf8(p));
    std::string s{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (in.bad()) return core::make_error(Errc::io_error, "cannot read " + utf8(p));
    return s;
}

std::string zip_error(mz_zip_archive& zip) { return mz_zip_get_error_string(mz_zip_get_last_error(&zip)); }

class ZipReader {
public:
    ZipReader() = default;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ~ZipReader() {
        if (open_) mz_zip_reader_end(&zip_);
    }
    bool open(const std::string& bytes) { return open_ = mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0); }
    mz_zip_archive& zip() { return zip_; }

private:
    mz_zip_archive zip_{};
    bool open_ = false;
};

class ExtractIter {
public:
    explicit ExtractIter(mz_zip_reader_extract_iter_state* s) : s_{s} {}
    ExtractIter(const ExtractIter&) = delete;
    ExtractIter& operator=(const ExtractIter&) = delete;
    ~ExtractIter() {
        if (s_) mz_zip_reader_extract_iter_free(s_);
    }
    explicit operator bool() const { return s_ != nullptr; }
    bool fill(std::string& buf, std::size_t from) {
        while (from < buf.size()) {
            const auto n = mz_zip_reader_extract_iter_read(s_, buf.data() + from, buf.size() - from);
            if (n == 0) return false;
            from += n;
        }
        return true;
    }

private:
    mz_zip_reader_extract_iter_state* s_;
};

core::Result<NpArray> read_member(mz_zip_archive& zip, mz_uint index, std::uint64_t size, const NpLimits& limits) {
    ExtractIter it{mz_zip_reader_extract_iter_new(&zip, index, 0)};
    if (!it) return fail("cannot read: " + zip_error(zip));
    std::string buf(static_cast<std::size_t>(std::min<std::uint64_t>(size, 12)), '\0');
    if (!it.fill(buf, 0)) return fail("truncated archive member");
    const auto byte = [&](std::size_t k) { return k < buf.size() ? static_cast<std::size_t>(static_cast<unsigned char>(buf[k])) : 0; };
    const std::size_t header = byte(6) == 1 ? 10 + (byte(8) | byte(9) << 8) : 12 + (byte(8) | byte(9) << 8 | byte(10) << 16 | byte(11) << 24);
    if (header > kMaxHeader + 12) return fail(".npy header is too long");
    const auto have = buf.size();
    buf.resize(static_cast<std::size_t>(std::min<std::uint64_t>(size, header)));
    if (buf.size() > have && !it.fill(buf, have)) return fail("truncated archive member");
    const auto h = parse_header(buf, limits);
    if (!h) return std::unexpected{h.error()};
    const auto total = h->data_offset + h->count * h->item();
    if (total > size) return fail("truncated .npy data: expected " + std::to_string(h->count) + " values");
    constexpr std::size_t kChunk = std::size_t{1} << 20;
    while (buf.size() < total) {
        const auto from = buf.size();
        buf.resize(from + std::min(kChunk, total - from));
        if (!it.fill(buf, from)) return fail("truncated archive member");
    }
    return decode_npy(buf, limits);
}

}  // namespace

std::size_t np_count(const NpArray& a) {
    std::size_t n = 1;
    for (const auto d : a.shape) n *= d;
    return n;
}

core::Result<std::string> encode_npy(const NpArray& a) {
    std::size_t count = 1;
    for (const auto d : a.shape) {
        if (d != 0 && count > std::numeric_limits<std::size_t>::max() / d) return fail("npy shape is too large");
        count *= d;
    }
    std::string descr, payload;
    if (const auto* v = std::get_if<std::vector<double>>(&a.data)) {
        if (v->size() != count) return fail("npy array has " + std::to_string(v->size()) + " values for shape " + shape_text(a.shape));
        descr = "<f8";
        payload.reserve(count * 8);
        for (const double x : *v) put_le(payload, std::bit_cast<std::uint64_t>(x), 8);
    } else {
        const auto& texts = std::get<std::vector<std::string>>(a.data);
        if (texts.size() != count) return fail("npy array has " + std::to_string(texts.size()) + " strings for shape " + shape_text(a.shape));
        std::vector<std::vector<std::uint32_t>> all;
        std::size_t width = 1;
        for (const auto& s : texts) {
            auto cps = code_points(s);
            if (!cps) return fail("npy text is not valid UTF-8");
            width = std::max(width, cps->size());
            all.push_back(std::move(*cps));
        }
        descr = "<U" + std::to_string(width);
        payload.reserve(count * width * 4);
        for (const auto& cps : all)
            for (std::size_t k = 0; k < width; ++k) put_le(payload, k < cps.size() ? cps[k] : 0, 4);
    }
    std::string dict = "{'descr': '" + descr + "', 'fortran_order': False, 'shape': " + shape_text(a.shape) + ", }";
    while ((10 + dict.size() + 1) % 64 != 0) dict += ' ';
    dict += '\n';
    if (dict.size() > 0xFFFF) return fail("npy header is too long");
    std::string out{kMagic};
    out += '\x01';
    out += '\x00';
    put_le(out, dict.size(), 2);
    return out + dict + payload;
}

core::Result<NpArray> decode_npy(std::string_view bytes, const NpLimits& limits) {
    const auto h = parse_header(bytes, limits);
    if (!h) return std::unexpected{h.error()};
    const auto item = h->item();
    if (item != 0 && h->count > (bytes.size() - h->data_offset) / item)
        return fail("truncated .npy data: expected " + std::to_string(h->count) + " values");
    const char* p = bytes.data() + h->data_offset;
    const bool little = std::endian::native == std::endian::little;
    const bool swap = (h->dtype.order == '>' && little) || (h->dtype.order == '<' && !little);
    const bool reorder = h->fortran && h->shape.size() > 1;
    NpArray out;
    out.shape = h->shape;
    if (h->dtype.kind == 'U') {
        std::vector<std::string> v(h->count);
        for (std::size_t k = 0; k < h->count; ++k) v[reorder ? c_index(k, h->shape) : k] = read_text(p + k * item, h->dtype.size, swap);
        out.data = std::move(v);
    } else {
        std::vector<double> v(h->count);
        for (std::size_t k = 0; k < h->count; ++k) v[reorder ? c_index(k, h->shape) : k] = read_number(p + k * item, h->dtype, swap);
        out.data = std::move(v);
    }
    return out;
}

core::Result<void> write_npy(const std::filesystem::path& path, const NpArray& a) {
    const auto bytes = encode_npy(a);
    if (!bytes) return at(path, bytes.error());
    return write_file_atomic(path, *bytes);
}

core::Result<NpArray> read_npy(const std::filesystem::path& path, const NpLimits& limits) {
    const auto bytes = read_file(path);
    if (!bytes) return std::unexpected{bytes.error()};
    auto a = decode_npy(*bytes, limits);
    if (!a) return at(path, a.error());
    return a;
}

core::Result<void> write_npz(const std::filesystem::path& path, const NpzEntries& entries) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return core::make_error(Errc::io_error, utf8(path) + ": " + zip_error(zip));
    const auto bail = [&](std::unexpected<core::Error> e) {
        mz_zip_writer_end(&zip);
        return at(path, e.error());
    };
    std::set<std::string, std::less<>> seen;
    for (const auto& [key, array] : entries) {
        if (!seen.insert(key).second) return bail(fail("duplicate key '" + key + "'"));
        const auto bytes = encode_npy(array);
        if (!bytes) return bail(std::unexpected{bytes.error()});
        const auto member = key + ".npy";
        if (!mz_zip_writer_add_mem(&zip, member.c_str(), bytes->data(), bytes->size(), MZ_NO_COMPRESSION)) return bail(fail(zip_error(zip)));
    }
    void* buf = nullptr;
    std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buf, &size)) return bail(fail(zip_error(zip)));
    std::string bytes{static_cast<const char*>(buf), size};
    mz_free(buf);
    mz_zip_writer_end(&zip);
    return write_file_atomic(path, bytes);
}

core::Result<NpzEntries> read_npz(const std::filesystem::path& path, const NpLimits& limits) {
    const auto bytes = read_file(path);
    if (!bytes) return std::unexpected{bytes.error()};
    ZipReader reader;
    if (!reader.open(*bytes)) return at(path, core::Error{Errc::invalid_argument, "not an .npz archive: " + zip_error(reader.zip())});
    auto& zip = reader.zip();
    NpzEntries out;
    const auto n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) return at(path, core::Error{Errc::invalid_argument, zip_error(zip)});
        if (st.m_is_directory) continue;
        const std::string name = st.m_filename;
        if (!name.ends_with(".npy")) return at(path, core::Error{Errc::invalid_argument, "member '" + name + "' is not an .npy array"});
        auto a = read_member(zip, i, st.m_uncomp_size, limits);
        if (!a) return at(path, core::Error{a.error().code, name + ": " + a.error().message});
        out.emplace_back(name.substr(0, name.size() - 4), std::move(*a));
    }
    return out;
}

NpArray np_scalar(double v) { return {{}, std::vector<double>{v}}; }

NpArray np_text(std::string s) { return {{}, std::vector<std::string>{std::move(s)}}; }

NpArray np_vector(std::vector<double> v) {
    const auto n = v.size();
    return {{n}, std::move(v)};
}

NpArray np_matrix(const std::vector<std::vector<double>>& rows) {
    std::vector<double> flat;
    const std::size_t cols = rows.empty() ? 0 : rows.front().size();
    flat.reserve(rows.size() * cols);
    for (const auto& r : rows) flat.insert(flat.end(), r.begin(), r.end());
    return {{rows.size(), cols}, std::move(flat)};
}

const NpArray* np_find(const NpzEntries& entries, std::string_view key) {
    for (const auto& [k, a] : entries)
        if (k == key) return &a;
    return nullptr;
}

}  // namespace signals::io
