#include "InputStream.hpp"

#include <boost/format.hpp>
#include <stdexcept>

using boost::format;
using namespace std;

namespace {
    const unsigned maxCompressionMagic = 3;
    const struct CompressionAlgorithm {
        CompressionType alg;
        int len;
        const char* magic;
    } compressionMagic[] = {
        { GZIP, 2, "\037\213" },
        { BZIP2, 3, "BZh" }
    };
    unsigned nCompressionAlgs = sizeof(compressionMagic)/sizeof(compressionMagic[0]);
}


CompressionType compressionTypeFromString(const string& s) {
    using boost::format;

    if (s.empty() || s == "n")
        return NONE;
    else if (s == "g")
        return GZIP;
    else if (s == "b")
        return BZIP2;
    else
        throw runtime_error(str(format("Invalid compression string '%1%'. Expected one of: n,g,z,b") %s));
}

InputStream::ptr InputStream::create(const string& name, istream& rawStream) {
    return ptr(new InputStream(name, rawStream));
}

InputStream::InputStream(const string& name, istream& rawStream)
    : _name(name)
    , _rawStream(rawStream)
    , _caching(false)
    , _cacheIter(_cache.begin())
    , _lineNum(0)
{
    switch (detectCompression()) {
        case GZIP:
            _in.push(_gzipDecompressor);
            break;

        case BZIP2:
            _in.push(_bzip2Decompressor);
            break;

        default:
        case NONE:
            break;
    }

    _in.push(_rawStream);
}

void InputStream::caching(bool value) {
    _caching = value;
}

void InputStream::rewind() {
    _cacheIter = _cache.begin();
}

bool InputStream::getline(string& line) {
    if (_cacheIter != _cache.end()) {
        line = *_cacheIter++;
        return true;
    }

    // read until we get a line that isn't blank.
    while (std::getline(_in, line) && line.empty())
        ++_lineNum;
    ++_lineNum;

    if (_caching && _in) {
        _cache.push_back(line);
        _cacheIter = _cache.end();
    }

    return _in;
}


char InputStream::peek() const {
    if (_cacheIter != _cache.end())
        return (*_cacheIter)[0];

    char c(0);
    if (boost::iostreams::read(_in, &c, 1))
        boost::iostreams::putback(_in, c);
    return c;
}


bool InputStream::eof() const {
    return _cacheIter == _cache.end() && _in.eof();
}

uint64_t InputStream::lineNum() const {
    return _lineNum;
}

CompressionType InputStream::detectCompression() {
    CompressionType rv = NONE;
    char magic[maxCompressionMagic];
    for (unsigned i = 0; i < maxCompressionMagic; ++i)
        magic[i] = _rawStream.get();
    for (unsigned i = 0; i < nCompressionAlgs; ++i) {
        if (strncmp(compressionMagic[i].magic, magic, compressionMagic[i].len) == 0)
            rv = compressionMagic[i].alg;
    }
    for (unsigned i = 0; i < maxCompressionMagic; ++i)
        _rawStream.unget();

    return rv;
}
