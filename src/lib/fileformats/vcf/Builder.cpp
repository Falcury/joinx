#include "Builder.hpp"
#include "Entry.hpp"
#include "EntryMerger.hpp"
#include "GenotypeFormatter.hpp" // TODO: move DisjointAllelesException out of this header
#include "Header.hpp"

#include <iostream>
#ifdef DEBUG_VCF_MERGE
# include <iterator>
# include <set>
#endif // DEBUG_VCF_MERGE

using namespace std;

BEGIN_NAMESPACE(Vcf)

Builder::Builder(const MergeStrategy& mergeStrategy, Header* header, OutputFunc out)
    : _mergeStrategy(mergeStrategy)
    , _header(header)
    , _out(out)
{
}

Builder::~Builder() {
    flush();
}

void Builder::push(const Entry& e) {
    _entries.push_back(e);
}

void Builder::operator()(const Entry& e) {
    e.header();
    if (_entries.empty() || canMerge(e, _entries[0])) {
        push(e);
        return;
    }

    flush();
    push(e);
}

void Builder::output(const Entry* begin, const Entry* end) const {
    try {
        EntryMerger merger(_mergeStrategy, _header, begin, end);
        Entry merged(merger);
        _out(merged);
    } catch (const DisjointGenotypesError& e) {
        // TODO: real logging
        cerr << e.what() << "\nEntries:\n";
        // we'll pick the guy with sourceIndex = 0
        const Entry* chosen = begin;
        for (const Entry* ee = begin; ee != end; ++ee) {
            cerr << *ee << "\n";
            // TODO: NO NO NO NO NO NO take this hack out
            if (ee->header().sourceIndex() == 0)
                chosen = ee;
        }
        if (end - begin == 2) {
            cerr << "Going with entry #" << int(chosen-begin)+1 << ".\n";
            EntryMerger merger(_mergeStrategy, _header, chosen, chosen+1);
            Entry merged(merger);
            _out(merged);
        } else {
            cerr << "More than 2 entries, abort.\n";
            throw;
        }
    }

#ifdef DEBUG_VCF_MERGE
    if (merged.alt().size() > 1) {
        cerr << "MERGED ALLELES (" << _entries[0].chrom() << ", " << _entries[0].pos() << "): ";
        set<string> origAlleles;
        for (auto i = begin; i != end; ++i) {
            auto alts = i->alt();
            for (auto alt = alts.begin(); alt != alts.end(); ++alt) {
                origAlleles.insert(i->ref() + ":" + *alt);
            }
        }
        for (auto i = origAlleles.begin(); i != origAlleles.end(); ++i) {
            if (i != origAlleles.begin())
                cerr << " + ";
            cerr << *i;
        }
        
        cerr << " = " << merged.ref() << ":";
        auto alts = merged.alt();
        for (auto i = alts.begin(); i != alts.end(); ++i) {
            if (i != alts.begin())
                cerr << ",";
            cerr << *i;
        }
        cerr << "\n";
    }
#endif // DEBUG_VCF_MERGE
}


void Builder::flush() {
    if (!_entries.empty()) {
        output(&*_entries.begin(), &*_entries.end());
        _entries.clear();
    }
}

bool Builder::canMerge(const Entry& a, const Entry& b) {
    return a.chrom() == b.chrom() && a.pos() == b.pos();
}

END_NAMESPACE(Vcf)
