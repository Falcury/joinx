#include "IntersectionOutputFormatter.hpp"

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "fileformats/Bed.hpp"
#include "common/Tokenizer.hpp"
#include <boost/format.hpp>
#include <cstdlib>
#include <functional>


using boost::format;
using namespace std;

namespace IntersectionOutput {

class ColumnBase {
public:
    ColumnBase(std::ostream& s, unsigned which)
        : _which(which)
        , _extraFields(0)
        , _s(s)
    {}

    virtual ~ColumnBase() {}

    virtual void output(const Bed& a, const Bed& b) = 0;
    virtual unsigned which() const { return _which; }
    virtual unsigned extraFields() const { return _extraFields; }

protected:
    unsigned _which;
    unsigned _extraFields;
    std::ostream& _s;
};

class IntersectionColumns : public ColumnBase {
public:
    IntersectionColumns(std::ostream& s)
        : ColumnBase(s, 0)
    {}

    void output(const Bed& a, const Bed& b) {
        _s << a.chrom() << "\t" << std::max(a.start(), b.start()) << "\t"
            << std::min(a.stop(), b.stop());
    }
};

class Column : public ColumnBase {
public:
    Column(std::ostream& s, unsigned which, unsigned field)
        : ColumnBase(s, which)
        , _field(field)
    {
        switch (_field) {
            case 0:
                _output = boost::bind(&Column::outputChrom, this, _1, _2);
                break;
            case 1:
                _output = boost::bind(&Column::outputStart, this, _1, _2);
                break;
            case 2:
                _output = boost::bind(&Column::outputStop, this, _1, _2);
                break;
            default:
                _field -= 3;
                _extraFields = _field + 1;
                _output = boost::bind(&Column::outputExtraField, this, _1, _2);
                break;
        }
    }

    template<typename OutContainer>
    static void parse(const std::string& fmt, std::ostream& s, OutContainer& out) {
        unsigned which = 0;

        if (fmt.empty())
            throw runtime_error("Empty format string component");

        switch (fmt[0]) {
            case 'A': which = 0; break;
            case 'B': which = 1; break;
            default:
                throw runtime_error(str(format(
                    "Invalid format string component %1%") %fmt));
                break;
        }

        string remainder(&fmt[1]);
        Tokenizer<string> t(remainder, ",-");
        unsigned field(0), rangeEnd(0);
        while (t.extract(field)) {
            switch (t.lastDelim()) {
                case '\0':
                case ',':
                    out.push_back(new Column(s, which, field));
                    break;

                case '-':
                    if (!t.extract(rangeEnd) || rangeEnd < field)
                        throw runtime_error(str(format(
                            "Invalid format string component %1%") %fmt));

                    while (field <= rangeEnd)
                        out.push_back(new Column(s, which, field++));
                    break;
            }
        }
    }

    void output(const Bed& a, const Bed& b) {
        _output(a, b);
    }

    void outputChrom(const Bed& a, const Bed& b) {
        const Bed& bed = _which ? b : a;
        _s << bed.chrom();
    }

    void outputStart(const Bed& a, const Bed& b) {
        const Bed& bed = _which ? b : a;
        _s << bed.start();
    }

    void outputStop(const Bed& a, const Bed& b) {
        const Bed& bed = _which ? b : a;
        _s << bed.stop();
    }

    void outputExtraField(const Bed& a, const Bed& b) {
        const Bed& bed = _which ? b : a;
        if (_field >= bed.extraFields().size())
            throw runtime_error(str(format(
                "Field index '%1%' out of bounds") %(_field+3)));

        _s << bed.extraFields()[_field];
    }

protected:
    unsigned _field;
    boost::function<void(const Bed&, const Bed&)> _output;
};

class CompleteColumn : public ColumnBase {
public:
    CompleteColumn(std::ostream& s, unsigned which)
        : ColumnBase(s, which)
    {}

    void output(const Bed& a, const Bed& b) {
        _s << (_which ? b : a);
    }
};

Formatter::Formatter(const std::string& formatString, std::ostream& s)
    : _s(s)
{
    Tokenizer<char> tokenizer(formatString, ' ');
    string token;
    while (tokenizer.extract(token)) {
        if (token.empty())
            throw runtime_error(str(format("Invalid format string '%1%'")
                %formatString));

        if (token == "I") {
            _columns.push_back(new IntersectionColumns(s));
        } else if (token == "A") {
            _columns.push_back(new CompleteColumn(s, 0));
        } else if (token == "B") {
            _columns.push_back(new CompleteColumn(s, 1));
        } else {
            Column::parse(token, s, _columns);
        }
    }
}

Formatter::~Formatter() {
}

void Formatter::output(const Bed& a, const Bed& b) {
    for (unsigned i = 0; i < _columns.size(); ++i) {
        _columns[i].output(a, b);
        if (i < _columns.size() - 1) _s << "\t";
    }
    _s << "\n";
}

unsigned Formatter::extraFields(unsigned which) const {
    unsigned n = 0;
    for (auto i = _columns.begin(); i != _columns.end(); ++i)
        if (i->which() == 0)
            n = max(n, i->extraFields());
    return n;
}

}
