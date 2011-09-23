#include "CustomValue.hpp"

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

using boost::format;
using boost::lexical_cast;
using namespace std;

VCF_NAMESPACE_BEGIN

CustomValue::CustomValue()
    : _type(0)
{
}

CustomValue::CustomValue(const CustomType* type)
    : _type(type)
{
}

CustomValue::CustomValue(const CustomType* type, const string& value)
    : _type(type)
{
    if (value == ".")
        return;

    bool rv = false;
    switch (type->type()) {
        case CustomType::INTEGER:
            rv = set<int64_t>(value);
            break;

        case CustomType::FLOAT:
            rv = set<double>(value);
            break;

        case CustomType::CHAR:
            rv = set<char>(value);
            break;

        case CustomType::STRING:
            rv = set<string>(value);
            break;

        case CustomType::FLAG:
            // no need for an extra bool
            // the values very presence is an indication of its truthiness
            rv = true;
            break;

        default:
            throw runtime_error("Invalid custom VCF type!");
            break;
    }

    if (rv == false)
        throw runtime_error(str(format("Failed to coerce value '%1%' into %2% for field '%3%'")
            %value %CustomType::typeToString(type->type()) %type->id()));
}

const CustomValue::ValueType* CustomValue::getAny(SizeType idx) const {
    type().validateIndex(idx);
    if (idx >= _values.size())
        return 0;
    return &_values[idx];
}

const CustomType& CustomValue::type() const {
    if (!_type)
        throw runtime_error("Attempted to use Vcf CustomValue with uninitialized type");
    return *_type;
}

CustomValue::SizeType CustomValue::size() const {
    return _values.size();
}

bool CustomValue::empty() const {
    return _values.empty();
}

void CustomValue::setString(SizeType idx, const std::string& value) {
    type().validateIndex(idx);
    stringstream ss;
    if (value.empty() || value == ".") {
        _values.clear();
        return;
    }

    switch (type().type()) {
        case CustomType::INTEGER:
             set<int64_t>(value);
            break;

        case CustomType::FLOAT:
             set<double>(value);
            ss << *get<double>(idx);
            break;

        case CustomType::CHAR:
             set<char>(value);
            break;

        case CustomType::STRING:
             set<string>(value);
            break;

        case CustomType::FLAG:
            // no need for an extra bool
            // the values very presence is an indication of its truthiness
            break;

        default:
            throw runtime_error("Invalid custom VCF type!");
            break;
    }
}

std::string CustomValue::getString(SizeType idx) const {
    type().validateIndex(idx);
    stringstream ss;
    if (_values[idx].empty())
        return ".";

    switch (type().type()) {
        case CustomType::INTEGER:
             ss << *get<int64_t>(idx);
            break;

        case CustomType::FLOAT:
            ss << *get<double>(idx);
            break;

        case CustomType::CHAR:
            ss << *get<char>(idx);
            break;

        case CustomType::STRING:
            return *get<string>(idx);
            break;

        case CustomType::FLAG:
            // no need for an extra bool
            // the values very presence is an indication of its truthiness
            break;

        default:
            throw runtime_error("Invalid custom VCF type!");
            break;
    }

    return ss.str();
}

void CustomValue::toStream(ostream& s) const {
    for (SizeType i = 0; i < size(); ++i) {
        if (i > 0)
            s << ",";
        s << getString(i);
    }
}

std::string CustomValue::toString() const {
    stringstream ss;
    toStream(ss);
    return ss.str();
}

void CustomValue::append(const CustomValue& other) {
    if (other.type() != type())
        throw runtime_error(str(format("Attempted to concatenate conflicting custom types: %1% and %2%")
            %type().toString() %other.type().toString()));
    SizeType idx = size();
    SizeType newSize = size() + other.size();
    type().validateIndex(newSize-1);
    ensureCapacity(newSize);
    for (SizeType i = 0; i < other.size(); ++i)
        _values[idx++] = other._values[i];
}


VCF_NAMESPACE_END

std::ostream& operator<<(std::ostream& s, const Vcf::CustomValue& v) {
    return s;
}

