#include "Reader.hpp"

#include <boost/format.hpp>

using boost::format;

VCF_NAMESPACE_BEGIN

Reader::Reader(InputStream& in)
    : _in(in)
    , _cached(false)
    , _cachedRv(false)
{
    while (_in.getline(_buf) && !_buf.empty() && _buf[0] == '#') {
        _header.add(_buf);
    }

    try {
        _header.assertValid();
    } catch (const exception& e) {
        throw runtime_error(str(format("While reading vcf file %1%: %2%") %name() %e.what()));
    }
}

const Header& Reader::header() const {
    return _header;
}

bool Reader::eof() {
    return _buf.empty() && _in.eof();
}

bool Reader::next(Entry& e) {
    if (eof())
        return false;

    if (_buf.empty())
        _in.getline(_buf);

    e = Entry(_buf);
    _in.getline(_buf);

    return true;
}

bool Reader::peek(Entry** e) {
    if (!_cached)
        _cachedRv = next(_cachedEntry);
    if (_cachedRv)
        *e = &_cachedEntry;

    return _cachedRv;
}


VCF_NAMESPACE_END
