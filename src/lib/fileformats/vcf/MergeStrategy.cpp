#include "MergeStrategy.hpp"
#include "Entry.hpp"
#include "Header.hpp"
#include "CustomValue.hpp"
#include "fileformats/InputStream.hpp"
#include "common/Tokenizer.hpp"

#include <boost/format.hpp>
#include <functional>
#include <set>
#include <stdexcept>

using boost::format;
using namespace std;
using namespace std::placeholders;

BEGIN_NAMESPACE(Vcf)

typedef ValueMergers::Base::FetchFunc FetchFunc;

// parse a file with lines of the form: <info field id> = <strategy>, e.g.:
// DP=sum
void MergeStrategy::parse(InputStream& s) {
    string line;
    while (getline(s, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        Tokenizer<char> t(line, '=');
        string key;
        string value;
        t.extract(key);
        t.extract(value);
        setMerger(key, value);
    }
}

MergeStrategy::MergeStrategy(const Header* header, ConsensusFilter const* cnsFilt)
    : _header(header)
    , _default(0)
    , _registry(ValueMergers::Registry::getInstance())
    , _clearFilters(false)
    , _mergeSamples(false)
    , _primarySampleStreamIndex(0)
    , _cnsFilt(cnsFilt)
{
    _default = _registry->getMerger("ignore");
}

void MergeStrategy::clearFilters(bool value) {
    _clearFilters = value;
}

bool MergeStrategy::clearFilters() const {
    return _clearFilters;
}

void MergeStrategy::mergeSamples(bool value) {
    _mergeSamples = value;
}

bool MergeStrategy::mergeSamples() const {
    return _mergeSamples;
}

void MergeStrategy::primarySampleStreamIndex(uint32_t value) {
    _primarySampleStreamIndex = value;
}

uint32_t MergeStrategy::primarySampleStreamIndex() const {
    return _primarySampleStreamIndex;
}

void MergeStrategy::setMerger(const std::string& id, const std::string& mergerName) {
    if (_header->infoType(id) == NULL)
        throw runtime_error(str(format("Unknown datatype for info field '%1%'") %id));

    const ValueMergers::Base* merger = _registry->getMerger(mergerName);
    auto inserted = _info.insert(make_pair(id, merger));
    if (!inserted.second) {
        inserted.first->second = merger;
    }
}

const ValueMergers::Base* MergeStrategy::infoMerger(const string& which) const {
    const CustomValue* (Entry::*fetchInfo)(const string&) const = &Entry::info;
    FetchFunc fetch = bind(fetchInfo, _1, which);
    const CustomType* type = _header->infoType(which);
    if (!type)
        throw runtime_error(str(format("Unknown datatype for info field '%1%'") %which));

    auto iter = _info.find(which);
    if (iter != _info.end())
        return iter->second;
    else if (type->numberType() == CustomType::VARIABLE_SIZE)
        return _registry->getMerger("uniq-concat");
    else if (_default)
        return _default;
    else
        return _registry->getMerger("enforce-equal");
}

CustomValue MergeStrategy::mergeInfo(const string& which, const Entry* begin, const Entry* end) const {
    const CustomValue* (Entry::*fetchInfo)(const string&) const = &Entry::info;
    FetchFunc fetch = bind(fetchInfo, _1, which);
    const CustomType* type = _header->infoType(which);
    if (!type)
        throw runtime_error(str(format("Unknown datatype for info field '%1%'") %which));

    const ValueMergers::Base* merger = infoMerger(which);
    return (*merger)(type, fetch, begin, end);
}

ConsensusFilter const* MergeStrategy::consensusFilter() const {
    return _cnsFilt;
}

END_NAMESPACE(Vcf)
