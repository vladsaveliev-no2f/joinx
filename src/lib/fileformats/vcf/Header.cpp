#include "Header.hpp"
#include "Map.hpp"
#include "common/Tokenizer.hpp"

#include <boost/format.hpp>
#include <ctime>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

using boost::format;
using namespace std;
using namespace std::placeholders;

BEGIN_NAMESPACE(Vcf)

namespace {
    const char *expectedHeaderFields[] = {
        "CHROM", "POS", "ID", "REF", "ALT",
        "QUAL", "FILTER", "INFO", "FORMAT"
    };

    unsigned nExpectedHeaderFields = sizeof(expectedHeaderFields)/sizeof(expectedHeaderFields[0]);

    template<typename MapType>
    typename MapType::mapped_type const* mapGetPtr(MapType const& m, typename MapType::key_type const& key) {
        auto iter = m.find(key);
        if (iter == m.end())
            return 0;
        return &iter->second;
    }

    struct MetaInfoFilter {
        MetaInfoFilter() {}
        explicit MetaInfoFilter(const string& s) {
            targets.push_back(s);
        }

        void addTarget(const string& s) {
            targets.push_back(s);
        }

        bool operator()(const Header::RawLine& value) const {
            return find(targets.begin(), targets.end(), value.first) != targets.end();
        }

        vector<string> targets;
    };
}

Header::Header() : _headerSeen(false), _sourceIndex(0) {}
Header::~Header() {
}

void Header::add(const string& line) {
    // determine if this is a meta-information line (##) or header line (#)
    if (line[1] == '#') {
        // meta-info
        Tokenizer<char> t(line, '=');
        pair<string, string> p;
        if (!t.extract(p.first))
            throw runtime_error(str(format("Failed to parse VCF header line: %1%") %line));
        p.first = p.first.substr(2); // strip leading ##
        t.remaining(p.second);

        if (p.first == "INFO") {
            CustomType t(p.second.substr(1, p.second.size()-2));
            auto inserted = _infoTypes.insert(
                make_pair(t.id(), std::move(t))
            );
            if (!inserted.second)
                throw runtime_error(str(format("Duplicate value for INFO:%1%") %t.id()));
        } else if (p.first == "FORMAT") {
            CustomType t(p.second.substr(1, p.second.size()-2));
            auto inserted = _formatTypes.insert(
                make_pair(t.id(), std::move(t))
            );
            if (!inserted.second)
                throw runtime_error(str(format("Duplicate value for FORMAT:%1%") %t.id()));
        } else if (p.first == "FILTER") {
            // TODO: care about duplicates?
            Map m(p.second.substr(1, p.second.size()-2));
            _filters.insert(
                make_pair(m["ID"], m["Description"])
            );
        } else if (p.first == "SAMPLE") {
            SampleTag st(p.second.substr(1, p.second.size()-2));
            auto inserted = _sampleTags.insert(
                make_pair(st.id(), std::move(st))
            );
            if (!inserted.second)
                throw runtime_error(str(format("Duplicate SAMPLE ID in vcf header: %1%") %st.toString()));

        }

        _metaInfoLines.push_back(p);
    } else {
        parseHeaderLine(line.substr(1));
    }
}

void Header::addFilter(const string& id, const string& desc) {
    if (_filters.find(id) != _filters.end())
        throw runtime_error(str(format(
            "Attempted to add duplicate filter '%1%' to vcf header."
            ) %id));
    add(str(format("##FILTER=<ID=%1%,Description=\"%2%\">") %id %desc));
}

void Header::addInfoType(CustomType const& type) {
    add(str(format("##INFO=<%1%>") %type.toString()));
}

void Header::addFormatType(CustomType const& type) {
    add(str(format("##FORMAT=<%1%>") %type.toString()));
}

void Header::addSampleTag(SampleTag const& tag) {
    add(tag.toString());
}

void Header::parseHeaderLine(const std::string& line) {
    if (_headerSeen)
        throw runtime_error(str(format("Multiple header line detected:\n%1%\nAND\n%2%") %line %line));

    _headerSeen = true;
    Tokenizer<char> t(line, '\t');
    string tok;
    for (unsigned i = 0; i < nExpectedHeaderFields; ++i) {
        const char* expected = expectedHeaderFields[i];
        // once again we make special exceptions for dbsnp :|
        // they don't include FORMAT in the header of their vcf files
        bool extracted = t.extract(tok);
        if (!extracted && i == nExpectedHeaderFields-1)
            break;

        if (!extracted || tok != expected)
            throw runtime_error(str(format("Malformed header line: %1%\nExpected token: %2%") %line %expected));
    }

    set<string> seenNames;
    while (t.extract(tok)) {
        auto inserted = seenNames.insert(tok);
        if (!inserted.second)
            throw runtime_error(str(format(
                "Duplicate sample name in vcf header: %1%"
                ) %tok));
        _sampleNames.push_back(tok);
    }
}

inline std::string Header::headerLine() const {
    stringstream ss;
    ss << expectedHeaderFields[0];
    for (unsigned i = 1; i < nExpectedHeaderFields; ++i)
        ss << "\t" << expectedHeaderFields[i];
    for (auto iter = _sampleNames.begin(); iter != _sampleNames.end(); ++iter)
        ss << "\t" << *iter;
    return ss.str();
}


void Header::assertValid() const {
    if (_metaInfoLines.empty() || !_headerSeen)
        throw runtime_error("invalid or missing header");
}

void Header::merge(const Header& other, bool allowDuplicateSamples) {
    for (auto iter = other._sampleNames.begin(); iter != other._sampleNames.end(); ++iter) {
        auto exists = find(_sampleNames.begin(), _sampleNames.end(), *iter);
        size_t idx;
        if (exists != _sampleNames.end()) {
            if (!allowDuplicateSamples)
                throw runtime_error(str(format("Error merging VCF headers, sample name conflict: %1%") %*iter));
            idx = distance(_sampleNames.begin(), exists);
        } else {
            _sampleNames.push_back(*iter);
            idx = _sampleNames.size()-1;
        }
        if (idx >= _sampleSourceCounts.size())
            _sampleSourceCounts.resize(idx+1);
        ++_sampleSourceCounts[idx];
    }


    for (auto iter = other._metaInfoLines.begin(); iter != other._metaInfoLines.end(); ++iter) {
        // we already have that exact line
        if (find(_metaInfoLines.begin(), _metaInfoLines.end(), *iter) != _metaInfoLines.end())
            continue;
        add("##"+iter->first+"="+iter->second);
    }

    MetaInfoFilter filter("fileDate");
    auto newEnd = remove_if(_metaInfoLines.begin(), _metaInfoLines.end(), filter);
    _metaInfoLines.erase(newEnd, _metaInfoLines.end());
    char dateStr[32] = {0};
    time_t now = time(NULL);
    strftime(dateStr, sizeof(dateStr), "%Y%m%d", localtime(&now));
    add(str(format("##fileDate=%1%") %dateStr));
}

uint32_t Header::sampleIndex(const std::string& sampleName) const {
    auto iter = find(_sampleNames.begin(), _sampleNames.end(), sampleName);
    if (iter == _sampleNames.end())
        throw SampleNotFoundError(str(format("Request for sample name '%1%' which does not exist in this VCF header") %sampleName));
    return distance(_sampleNames.begin(), iter);
}

bool Header::empty() const {
    return _metaInfoLines.empty();
}

CustomType const* Header::infoType(const std::string& id) const {
    return mapGetPtr(_infoTypes, id);
}

CustomType const* Header::formatType(const std::string& id) const {
    return mapGetPtr(_formatTypes, id);
}

SampleTag const* Header::sampleTag(std::string const& id) const {
    return mapGetPtr(_sampleTags, id);
}

std::map<std::string, CustomType> const& Header::infoTypes() const {
    return _infoTypes;
}

std::map<std::string, CustomType> const& Header::formatTypes() const {
    return _formatTypes;
}

std::map<std::string, std::string> const& Header::filters() const {
    return _filters;
}

std::map<std::string, SampleTag> const& Header::sampleTags() const {
    return _sampleTags;
}

std::vector<Header::RawLine> const& Header::metaInfoLines() const {
    return _metaInfoLines;
}

std::vector<std::string> const& Header::sampleNames() const {
    return _sampleNames;
}

std::vector<size_t> const& Header::sampleSourceCounts() const {
    return _sampleSourceCounts;
}

void Header::mirrorSample(std::string const& sampleName, std::string const& newName) {
    auto newExists = find(_sampleNames.begin(), _sampleNames.end(), newName);
    if (newExists != _sampleNames.end()) {
        throw runtime_error(str(format(
            "Attempted to mirror sample '%1%' as '%2%', but sample '%2%' already exists"
            ) %sampleName %newName));
    }

    size_t targetIdx = sampleIndex(sampleName);
    size_t newIdx = _sampleNames.size();
    _sampleNames.push_back(newName);
    auto inserted = _mirroredSamples.insert(make_pair(newIdx, targetIdx));
    if (!inserted.second) {
        throw runtime_error(str(format(
            "Attempted to mirror sample %1% with name %2% which already exists!"
            ) %sampleName %newName));
    }
}

std::map<size_t, size_t> const& Header::mirroredSamples() const {
    return _mirroredSamples;
}

END_NAMESPACE(Vcf)

std::ostream& operator<<(std::ostream& s, const Vcf::Header& h) {
    const vector<Vcf::Header::RawLine>& mlines = h.metaInfoLines();
    for (auto i = mlines.begin(); i != mlines.end(); ++i)
        s << "##" << i->first << "=" << i->second << "\n";

    if (!h.headerLine().empty())
        s << '#' << h.headerLine() << "\n";

    return s;
}
