#include "Metrics.hpp"
#include "fileformats/vcf/Header.hpp"
#include "common/Sequence.hpp"

#include <boost/bind.hpp>

#include <locale>
#include <string>
#include <boost/format.hpp>
#include <stdexcept>
#include <functional>
#include <numeric>

using boost::format;
using namespace std;

BEGIN_NAMESPACE(Metrics)
namespace {
    bool customTypeIdMatches(string const& id, Vcf::CustomType const* type) {
        return type && type->id() == id;
    }

    bool minorAlleleSort (int i,int j) { return (i != 0 && i<j); }

    bool isRefOrNull(Vcf::GenotypeIndex const& gtidx) {
        return gtidx == Vcf::GenotypeIndex::Null || gtidx.value == 0;
    }
}

EntryMetrics::EntryMetrics(Vcf::Entry const& entry, std::vector<std::string> const& novelInfoFields)
    : _maxGtIdx(0)
    , _entry(entry)
    , _novelInfoFields(novelInfoFields)
{
    identifyNovelAlleles();
    calculateGenotypeDistribution();
    calculateAllelicDistribution();
    calculateAllelicDistributionBySample();
    calculateMutationSpectrum();
}

void EntryMetrics::calculateGenotypeDistribution() {
    // convenience
    auto const& sd = _entry.sampleData();

    for (auto i = sd.begin(); i != sd.end(); ++i) {
        auto const& sampleIdx = i->first;

        // skip filtered samples
        if (sd.isSampleFiltered(sampleIdx))
            continue;

        Vcf::GenotypeCall const& gt = sd.genotype(sampleIdx);
        if(gt.size() == 0) {
            continue;
        }

        if (!gt.diploid()) {
            // anything but diploid is not supported until we understand a bit
            // better how to represent them
            cerr << "Non-diploid genotype for sample " <<
                _entry.header().sampleNames()[sampleIdx]
                << " skipped at position "
                << _entry.chrom() << "\t" << _entry.pos() << endl;
            continue;
        }
        else {
            ++_genotypeDistribution[gt];
            _maxGtIdx = _entry.alt().size(); //std::max(_maxGtIdx, *gt.indexSet().rbegin());
        }
    }
}

bool EntryMetrics::singleton(const Vcf::GenotypeCall* geno) const {
    for (auto i = geno->indexSet().begin(); i != geno->indexSet().end(); ++i) {
        // FIXME: should we count these too?
        if (*i == Vcf::GenotypeIndex::Null)
            continue;

        if(_allelicDistributionBySample[i->value] == 1) {
            //at least one of the alleles is a singleton
            return true;
        }
    }
    return false;
}

/*
bool EntryMetrics::novel(const Vcf::Entry& entry, const std::vector<std::string>& novelInfoFields, const Vcf::GenotypeCall* geno) {
    // grab alt allele index(es)
    // check to see if novel databases have seen them
    // return depending on that

    bool novel = true;
    for( auto i = geno->indexSet().begin(); i != geno->indexSet().end(); ++i) {
        if(*i != 0) {
            for( auto j = novelInfoFields.begin(); j != novelInfoFields.end(); ++j) {
                entry.getInfoField(*j)

    }
    return novel;
}
*/

void EntryMetrics::calculateAllelicDistribution() {
    _allelicDistribution.resize(_maxGtIdx + 1);
    auto const& gtd = _genotypeDistribution;
    for( auto i = gtd.begin(); i != gtd.end(); ++i) {
        for(auto j = i->first.begin(); j != i->first.end(); ++j) {
            if (*j == Vcf::GenotypeIndex::Null)
                continue; // FIXME: should we count these too?

            _allelicDistribution[j->value] += i->second;
        }
    }
}

void EntryMetrics::calculateAllelicDistributionBySample() {
    _allelicDistributionBySample.resize(_maxGtIdx + 1);
    for( auto geno = _genotypeDistribution.begin(); geno != _genotypeDistribution.end(); ++geno) {
        for(auto i = (geno->first).indexSet().begin(); i != (geno->first).indexSet().end(); ++i) {
            if (*i == Vcf::GenotypeIndex::Null)
                continue; // FIXME: should we count these too?

            _allelicDistributionBySample[i->value] += geno->second;
        }
    }
}


void EntryMetrics::calculateMutationSpectrum() {
    _transitionByAlt.resize(_maxGtIdx);
    
    locale loc;
    std::string ref(_entry.ref());

    bool complement = false;

    if(ref.size() == 1) {
        toupper(ref[0], loc);

        if(ref == "G" || ref == "T") {
            complement = true;
            ref = Sequence::reverseComplement(ref);
        }

        for(auto geno = _genotypeDistribution.begin(); geno != _genotypeDistribution.end(); ++geno) {
            for(auto i = (geno->first).indexSet().begin(); i != (geno->first).indexSet().end(); ++i) {
                if (isRefOrNull(*i))
                    continue;

                std::string variant( _entry.alt()[i->value - 1] );
                if (variant.size() != 1)
                    //it's an indel and we want to skip it. Not freak out
                    continue;

                toupper(variant[0],loc);
                if(complement) {
                    variant = Sequence::reverseComplement(variant);
                }
                if(singleton(&geno->first)) {
                    _singletonMutationSpectrum(ref[0],variant[0]) += geno->second;
                }
                else {
                    _mutationSpectrum(ref[0],variant[0]) += geno->second;
                }
                //enter into by Allele array
                if( (ref[0] == 'A' && variant[0] == 'G') || (ref[0] == 'C' && variant[0] == 'T')) {
                    //it's a transition
                    _transitionByAlt[i->value - 1] = true;
                }
                else {
                    _transitionByAlt[i->value - 1] = false;
                }
            }
        }
    }
}

void EntryMetrics::identifyNovelAlleles() {
    //this will populate the novelty of each alt allele in _novelByAllele
    uint32_t numAlts = uint32_t(_entry.alt().size());
    _novelByAlt.resize(numAlts,true); //make space for alt novel status

    for (auto i = _novelInfoFields.begin(); i != _novelInfoFields.end(); ++i) {
        //for each database of variants, use to determine if an alt has been seen
        const Vcf::CustomValue* database = _entry.info(*i);  //search for tag
        if(database) {
            //if we find it check if it's per alt or a flag. Otherwise fail miserably.
            if(database->size() == numAlts) {
                //we know we have the same number of values as alts
                for(Vcf::CustomValue::SizeType j = 0; j != _novelByAlt.size(); ++j) {
                    _novelByAlt[j] = _novelByAlt[j] && database->getAny(j) == 0;
                }
            }
            else {
                //do nothing for now
                //could have been that the person did not specify perAlt
                //What do we do then?
            }
        }
    }
}

double EntryMetrics::minorAlleleFrequency() const {
    if(!_allelicDistribution.empty()) {

        // this is iterator guaranteed to be valid since _allelicDistribution cannot be empty
        auto minorAlleleIter = min_element(
            _allelicDistribution.begin(), _allelicDistribution.end(),
            minorAlleleSort);

        auto num = *minorAlleleIter;
        auto totalAlleles = accumulate(_allelicDistribution.begin(), _allelicDistribution.end(), 0);
        if (num == 0 || totalAlleles == 0)
            return 0;

        return double(num)/totalAlleles;
    }
    else {
        throw runtime_error("Unable to calculate minorAlleleFrequency if the allelic distribution is empty");
    }
}

const std::vector<double> EntryMetrics::alleleFrequencies() const {
    if(!_allelicDistribution.empty()) {
        uint32_t totalAlleles = accumulate(_allelicDistribution.begin(), _allelicDistribution.end(), 0);
        std::vector<double> frequencies;
        for(auto allele = _allelicDistribution.begin(); allele != _allelicDistribution.end(); ++allele) {
            double value = 0.0;
            if (totalAlleles != 0) {
                value = (double) *allele / totalAlleles;
            }
            frequencies.push_back(value);
        }
        return frequencies;
    }
    else {
        throw runtime_error("Unable to calculate minorAlleleFrequency if the allelic distribution is empty");
    }
}

const std::vector<bool>& EntryMetrics::transitionStatusByAlt() const {
    return _transitionByAlt;
}

const std::vector<bool>& EntryMetrics::novelStatusByAlt() const {
    return _novelByAlt;
}

const MutationSpectrum& EntryMetrics::mutationSpectrum() const {
    return _mutationSpectrum;
}

const MutationSpectrum& EntryMetrics::singletonMutationSpectrum() const {
    return _singletonMutationSpectrum;
}

const std::map<Vcf::GenotypeCall const,uint32_t>& EntryMetrics::genotypeDistribution() const {
    return _genotypeDistribution;
}

const std::vector<uint32_t>& EntryMetrics::allelicDistribution() const {
    return _allelicDistribution;
}

const std::vector<uint32_t>& EntryMetrics::allelicDistributionBySample() const {
    return _allelicDistributionBySample;
}

SampleMetrics::SampleMetrics(size_t sampleCount)
    : _totalSites(0)
    , _perSampleHetVariants(sampleCount, 0)
    , _perSampleHomVariants(sampleCount, 0)
    , _perSampleRefCall(sampleCount, 0)
    , _perSampleFilteredCall(sampleCount, 0)
    , _perSampleCalls(sampleCount, 0)
    , _perSampleNonDiploidCall(sampleCount, 0)
    , _perSampleSingletons(sampleCount, 0)
    , _perSampleVeryRareVariants(sampleCount, 0)
    , _perSampleRareVariants(sampleCount, 0)
    , _perSampleCommonVariants(sampleCount, 0)
    , _perSampleKnownVariants(sampleCount, 0)
    , _perSampleNovelVariants(sampleCount, 0)  
    , _perSampleMutationSpectrum(sampleCount)
{
}

void SampleMetrics::processEntry(Vcf::Entry& e, EntryMetrics& m) {
    ++_totalSites;

    // convenience
    auto const& sd = e.sampleData();
    auto const& fmt = sd.format();
    uint64_t offset = distance(fmt.begin(), find_if(fmt.begin(), fmt.end(),
            boost::bind(&customTypeIdMatches, "FT", _1)));

    locale loc;
    std::string ref(e.ref());   //for mutation spectrum

    bool canCalcSpectrum = false;
    bool complement = false;

    if(e.ref().size() == 1) {
        canCalcSpectrum = true;
        toupper(ref[0],loc);
        if(ref == "G" || ref == "T") {
            complement = true;
            ref = Sequence::reverseComplement(ref);
        }
    }

    for (auto i = sd.begin(); i != sd.end(); ++i) {
        auto const& sampleIdx = i->first;
        if (i->second == 0)
            continue;
        auto const& values = *i->second;

        if (values.size() > offset) {
            const std::string *filter(values[offset].get<std::string>(0));
            if (filter != 0 && *filter != "PASS") {
                ++_perSampleFilteredCall[sampleIdx];
                continue;
            }
        }

        //if no FT then we assume all have passed :-(
        Vcf::GenotypeCall const& gt = e.sampleData().genotype(sampleIdx);
        if(gt.size() == 0) {
            //++_perSampleMissingCall[sampleIdx];
            continue;
        }

        ++_perSampleCalls[sampleIdx];

        if (!gt.diploid()) {
            //anything but diploid is not supported until we understand a bit better how to represent them
            cerr << "Non-diploid genotype for sample " << e.header().sampleNames()[sampleIdx] << " skipped at position " << e.chrom() << "\t" << e.pos() << endl;
            ++_perSampleNonDiploidCall[sampleIdx];
            continue;
        }
        else {
            if(gt.reference()) {
                ++_perSampleRefCall[sampleIdx];
                continue;
            }
            else if(gt.heterozygous()) {
                ++_perSampleHetVariants[sampleIdx];
            }
            else {
                ++_perSampleHomVariants[sampleIdx];
            }

            double maf = m.minorAlleleFrequency();
            if(m.singleton(&gt)) {
                ++_perSampleSingletons[sampleIdx];
            }
            else if(maf < 0.01) {
                ++_perSampleVeryRareVariants[sampleIdx];
            }
            else if(maf >= 0.01 && maf < 0.05) {
                ++_perSampleRareVariants[sampleIdx];
            }
            else {
                ++_perSampleCommonVariants[sampleIdx];
            }

            if(canCalcSpectrum) {
                for(auto j = gt.indexSet().begin(); j != gt.indexSet().end(); ++j) {
                    if(isRefOrNull(*j))
                        continue;
                    std::string variant( e.alt()[j->value - 1] );
                    if (variant.size() != 1)
                        //throw runtime_error(str(format("Invalid variant for ref entry %1%: %2%") %ref %variant));
                        continue;
                    toupper(variant[0],loc);
                    if(complement)
                        variant = Sequence::reverseComplement(variant);
                    //now have mutation class so go ahead and record
                    _perSampleMutationSpectrum[sampleIdx](ref[0], variant[0]) += 1;
                }
            }

            //determine if novel which is by allele
            std::vector<bool> novelByAlt = m.novelStatusByAlt();
            for(auto j = gt.indexSet().begin(); j != gt.indexSet().end(); ++j) {
                if(isRefOrNull(*j))
                    continue;
                if(novelByAlt[j->value - 1]) {    //need to subtract one because reference is not an Alt
                    ++_perSampleNovelVariants[sampleIdx];
                }
                else {
                    ++_perSampleKnownVariants[sampleIdx];
                }
            }
        }
    }
}

uint32_t SampleMetrics::numHetVariants(uint32_t index) const {
    return _perSampleHetVariants[index];
}

uint32_t SampleMetrics::numHomVariants(uint32_t index) const {
    return _perSampleHomVariants[index];
}

uint32_t SampleMetrics::numRefCalls(uint32_t index) const {
    return _perSampleRefCall[index];
}
uint32_t SampleMetrics::numFilteredCalls(uint32_t index) const {
    return _perSampleFilteredCall[index];
}
uint32_t SampleMetrics::numMissingCalls(uint32_t index) const {
    return _totalSites - _perSampleCalls[index] - _perSampleFilteredCall[index];
}
uint32_t SampleMetrics::numNonDiploidCalls(uint32_t index) const {
    return _perSampleNonDiploidCall[index];
}
uint32_t SampleMetrics::numSingletonVariants(uint32_t index) const {
    return _perSampleSingletons[index];
}
uint32_t SampleMetrics::numVeryRareVariants(uint32_t index) const {
    return _perSampleVeryRareVariants[index];
}
uint32_t SampleMetrics::numRareVariants(uint32_t index) const {
    return _perSampleRareVariants[index];
}
uint32_t SampleMetrics::numCommonVariants(uint32_t index) const {
    return _perSampleCommonVariants[index];
}
uint32_t SampleMetrics::numKnownVariants(uint32_t index) const {
    return _perSampleKnownVariants[index];
}
uint32_t SampleMetrics::numNovelVariants(uint32_t index) const {
    return _perSampleNovelVariants[index];
}

MutationSpectrum const& SampleMetrics::mutationSpectrum(uint32_t index) const {
    return _perSampleMutationSpectrum[index];
}

END_NAMESPACE(Metrics)

