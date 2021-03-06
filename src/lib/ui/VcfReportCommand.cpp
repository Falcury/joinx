#include "VcfReportCommand.hpp"

#include "common/Exceptions.hpp"
#include "common/MutationSpectrum.hpp"
#include "common/compat.hpp"
#include "fileformats/TypedStream.hpp"
#include "fileformats/vcf/CustomType.hpp"
#include "fileformats/vcf/CustomValue.hpp"
#include "fileformats/vcf/Entry.hpp"
#include "fileformats/vcf/GenotypeCall.hpp"
#include "fileformats/vcf/Header.hpp"
#include "io/InputStream.hpp"
#include "io/StreamHandler.hpp"
#include "metrics/Metrics.hpp"

#include <boost/program_options.hpp>

#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <limits>
#include <stdexcept>

namespace po = boost::program_options;
using namespace std;

VcfReportCommand::VcfReportCommand()
    : _infile("-")
    , _perSampleFile("per_sample_report.txt")
    , _perSiteFile("per_site_report.txt")
{
}

void VcfReportCommand::configureOptions() {
    _opts.add_options()
        ("input-file,i",
            po::value<string>(&_infile)->default_value("-"),
            "input file (omit or use '-' for stdin)")

        ("per-sample-file,S",
            po::value<string>(&_perSampleFile)->default_value("per_sample_report.txt"),
            "per sample report output file")

        ("per-site-file,s",
            po::value<string>(&_perSiteFile)->default_value("per_site_report.txt"),
            "per site report output file")

        ("info-fields-from-db,I",
            po::value<vector<string>>(&_infoFields),
            "info fields to use for determining if a variant is known (default: none)")
        ;

    _posOpts.add("input-file", 1);
}

void VcfReportCommand::exec() {
    InputStream::ptr instream(_streams.openForReading(_infile));
    ostream* perSampleOut = _streams.get<ostream>(_perSampleFile);
    ostream* perSiteOut = _streams.get<ostream>(_perSiteFile);
    if (_streams.cinReferences() > 1)
        throw runtime_error("stdin listed more than once!");
    uint32_t totalSites = 0;
    auto reader = openStream<Vcf::Entry>(*instream);
    Vcf::Entry entry;
    Metrics::SampleMetrics sampleMetrics(reader->header().sampleCount());

    *perSiteOut << "Chrom\tPos\tRef\tAlt\tTotalSamples\tNumberFiltered\tNumberMissing\tByAltTransition\tTotalTransitions\tTotalTransversions\tByAltNovel\tTotalNovel\tTotalKnown\tGenotypeDist\tAlleleDistBySample\tAlleleDist\tByAltAlleleFreq\tMAF\n"; 
    while (reader->next(entry)) {
        if (entry.alt().empty() ||
            (entry.failedFilters().size() >= 1 && entry.failedFilters().find("PASS") == entry.failedFilters().end()))

            continue;
        totalSites++;
        if(!entry.sampleData().hasGenotypeData())
            continue;

        std::unique_ptr<Metrics::EntryMetrics> pSiteMetrics;
        try {
            pSiteMetrics = std::make_unique<Metrics::EntryMetrics>(entry, _infoFields);
        } catch (InvalidAlleleError const& e) {
            std::cerr << e.what() << "\nSkipping entry " << entry << "\n";
            continue;
        }
        auto& siteMetrics = *pSiteMetrics;

        //output per-site metrics
        *perSiteOut << entry.chrom() << "\t" << entry.pos() << "\t" << entry.ref() << "\t";
        auto altIter = entry.alt().begin();
        while(altIter+1 != entry.alt().end()) {
            *perSiteOut << *(altIter++) << ",";
        }
        *perSiteOut << *(altIter);
        //transition status per allele will go next and then number of transitions at site and number of transversions at site
        *perSiteOut << "\t";

        uint32_t nSamples = entry.sampleData().header().sampleCount();
        *perSiteOut << nSamples << "\t"
            << entry.sampleData().samplesFailedFilter() << "\t"
            << entry.sampleData().samplesWithoutGenotypes() << "\t";

        std::vector<bool> transitionStatus = siteMetrics.transitionStatusByAlt();
        uint32_t transitionIdx = 0;
        uint32_t totalTransitionAlleles = 0;
        if(!transitionStatus.empty()) {
            transitionStatus = siteMetrics.transitionStatusByAlt();

            while(transitionIdx < (transitionStatus.size() - 1)) {
                totalTransitionAlleles +=  transitionStatus[transitionIdx];
                *perSiteOut << transitionStatus[transitionIdx++] << ",";
            }
            totalTransitionAlleles += transitionStatus[transitionIdx];
            *perSiteOut << transitionStatus[transitionIdx] << "\t" << totalTransitionAlleles << "\t" << transitionStatus.size() - totalTransitionAlleles;
        }
        else {
            *perSiteOut << "0\t" << totalTransitionAlleles << "\t" << transitionStatus.size() - totalTransitionAlleles;
        }

        //next novelness will go followed by number novel, number known
        *perSiteOut << "\t";
        std::vector<bool> novelStatus = siteMetrics.novelStatusByAlt();
        uint32_t novelIdx = 0;
        uint32_t totalNovelAlleles = 0;
        while(novelIdx < (novelStatus.size() - 1)) {
            totalNovelAlleles +=  novelStatus[novelIdx];
            *perSiteOut << novelStatus[novelIdx++] << ",";
        }
        totalNovelAlleles += novelStatus[novelIdx];
        *perSiteOut << novelStatus[novelIdx] << "\t" << totalNovelAlleles << "\t" << novelStatus.size() - totalNovelAlleles;

        //next genotype distribution
        //FIXME this will likely only work as expected if our genotypes are unphased and always diploid.
        auto const& allelesBySample = siteMetrics.allelicDistributionBySample();
        auto const& distribution = siteMetrics.genotypeDistribution();

        *perSiteOut << "\t";
        for(uint32_t index1 = 0; index1 < allelesBySample.size(); ++index1) {
            for(uint32_t index2 = 0; index2 <= index1; ++index2) {
                stringstream unphasedGenotype;
                unphasedGenotype << index2 << "/" << index1;
                Vcf::GenotypeCall gt(unphasedGenotype.str());        
                //*perSiteOut << gt.string() << ":";

                uint32_t count = 0;
                auto iter = distribution.find(gt);
                if (iter != distribution.end()) {
                    count = iter->second;
                }
                *perSiteOut << count;
                //FIXME this is undoubtedly bad
                if( (index1 + 1) < allelesBySample.size() || index2 < index1 ) {
                    *perSiteOut << ","; 
                }
            }
        }

        //this hits up the allele distribution from above. We grabbed it there so we could know how many alts there were.
        *perSiteOut << "\t";
        uint32_t bySampleIndex = 0;
        while(bySampleIndex < (allelesBySample.size() - 1)) {
            *perSiteOut << allelesBySample[bySampleIndex++] << ",";
        }
        *perSiteOut << allelesBySample[bySampleIndex];

        *perSiteOut << "\t";
        std::vector<uint32_t> alleles = siteMetrics.allelicDistribution();
        uint32_t alleleIndex = 0;
        while(alleleIndex < (alleles.size() - 1)) {
            *perSiteOut  << alleles[alleleIndex++] << ",";
        }
        *perSiteOut << alleles[alleleIndex];

        *perSiteOut << "\t";
        std::vector<double> frequencies = siteMetrics.alleleFrequencies();
        uint32_t freqIndex = 0;
        while(freqIndex < (frequencies.size() - 1)) {
            *perSiteOut << frequencies[freqIndex++] << ",";
        }
        *perSiteOut << frequencies[freqIndex];
        *perSiteOut << "\t" << siteMetrics.minorAlleleFrequency() << endl;


        sampleMetrics.processEntry(entry,siteMetrics);

        //plotting the above distribution in R
        //ggplot(x, aes(x=V7,y = ..count../sum(..count..))) + geom_histogram() + xlab("Minor Allele Frequency") + ylab("Frequency") + opts(title = "Minor Allele Frequency Distribution")
        //ggplot(x, aes(x=V7)) + geom_histogram() + xlab("Minor Allele Frequency") + ylab("Count") + opts(title = "Minor Allele Frequency Distribution")

        //next want to add to per-sample variant metrics
        //use MAF and distribution to classify
        //if singleton (ie this is the only sample to have that particular alt. index here then go in singleton bini
        //if not singleton but MAF < 1% then rare
        //if not singleton or rarest but MAF < 5% then less rare
        //if MAF >= 5% then common
        //for each sample report total number of no data, filtered, pass_filter, singleton, rarest, rare, common SNPs, in dbSNP, Singleton Transitions, Singleton Transversions, Non-singleton Transitions, Non-singleton Transversions

        // how many samples have a non-reference genotype?
        //cout << samplesWithNonRefGenotypes(entry) << "\n";
    }

    *perSampleOut << "SampleName\tTotalSites\tRef\tHet\tHom\tFilt\tMissing\tnonDiploid\tKnown\tNovel\tPercKnown\tSingleton\tVeryRare\tRare\tCommon\tTransitions\tTransversions\tTransition:Transversion" << endl;
    for(uint32_t i = 0; i < entry.header().sampleCount(); ++i) {
        *perSampleOut << entry.header().sampleNames()[i];
        *perSampleOut << "\t" << totalSites;
        *perSampleOut << "\t" << sampleMetrics.numRefCalls(i);
        *perSampleOut << "\t" << sampleMetrics.numHetVariants(i);
        *perSampleOut << "\t" << sampleMetrics.numHomVariants(i);
        *perSampleOut << "\t" << sampleMetrics.numFilteredCalls(i);
        *perSampleOut << "\t" << sampleMetrics.numMissingCalls(i);
        *perSampleOut << "\t" << sampleMetrics.numNonDiploidCalls(i);
        *perSampleOut << "\t" << sampleMetrics.numKnownVariants(i);
        *perSampleOut << "\t" << sampleMetrics.numNovelVariants(i);
        double fracKnown = (double) sampleMetrics.numKnownVariants(i) / (double)(sampleMetrics.numKnownVariants(i) + sampleMetrics.numNovelVariants(i));
        if(fracKnown != std::numeric_limits<double>::infinity()) {
            *perSampleOut << "\t" << fracKnown * 100 ;
        }
        else {
            *perSampleOut << "\tNA";
        }

        *perSampleOut << "\t" << sampleMetrics.numSingletonVariants(i);
        *perSampleOut << "\t" << sampleMetrics.numVeryRareVariants(i);
        *perSampleOut << "\t" << sampleMetrics.numRareVariants(i);
        *perSampleOut << "\t" << sampleMetrics.numCommonVariants(i);
        MutationSpectrum const& spectrum = sampleMetrics.mutationSpectrum(i);
        *perSampleOut << "\t" << spectrum.transitions();
        *perSampleOut << "\t" << spectrum.transversions();
        double ratio = spectrum.transitionTransversionRatio();
        if(ratio != std::numeric_limits<double>::infinity()) {
            *perSampleOut << "\t" << ratio;
        }
        else {
            *perSampleOut << "\tNA";
        }
        *perSampleOut << endl;
    }
    //cout << "Total number of segregating sites: " << totalSites << endl;
}
