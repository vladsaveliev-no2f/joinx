#pragma once

#include "Entry.hpp"
#include "GenotypeCall.hpp"
#include "Header.hpp"
#include "RawVariant.hpp"
#include "common/Region.hpp"
#include "common/TupleHasher.hpp"
#include "common/cstdint.hpp"
#include "common/namespaces.hpp"
#include "io/StreamJoin.hpp"

#include <boost/functional/hash.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/unordered_map.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <tuple>

BEGIN_NAMESPACE(Vcf)

class IGenotypeMatcher {
public:
    virtual ~IGenotypeMatcher() {}

    virtual void add(
            size_t sampleIdx,
            RawVariant::Vector const& alleles,
            size_t streamIdx,
            Vcf::Entry const* e) = 0;
    virtual void report(std::string const& sequence) = 0;
};

template<typename OutputWriter>
class ExactGenotypeMatcher : public IGenotypeMatcher {
public:
    typedef std::vector<
        boost::unordered_map<RawVariant::Vector, std::map<size_t, Entry const*>>
    > MapType;

    ExactGenotypeMatcher(size_t numSamples, OutputWriter& out)
        : gtmap_(numSamples)
        , out_(out)
    {
    }

    void add(
            size_t sampleIdx,
            RawVariant::Vector const& alleles,
            size_t streamIdx,
            Vcf::Entry const* e)
    {
        gtmap_[sampleIdx][alleles][streamIdx] = e;
    }

    void report(std::string const& sequence) {
        for (size_t sampleIdx = 0; sampleIdx < gtmap_.size(); ++sampleIdx) {
            auto& sd = gtmap_[sampleIdx];
            for (auto i = sd.begin(); i != sd.end(); ++i) {
                auto const& rawvs = i->first;
                auto const& who = i->second;
                out_(sampleIdx, sequence, rawvs, who);
            }
            sd.clear();
        }
    }

private:
    MapType gtmap_;
    OutputWriter& out_;
};

template<typename OutputWriter>
class FuzzyGenotypeMatcher : public IGenotypeMatcher {
public:
    enum {
        POS = 0,
        REF = 1
    };

    typedef std::tuple<int64_t, std::string> PosRefAllele;
    typedef std::string AltAllele;

    // x[ref/pos][alt][streamIdx] -> entry
    typedef std::vector<
        boost::unordered_map<
              PosRefAllele
            , boost::unordered_map<AltAllele, std::map<size_t, Entry const*>>
            , TupleHasher<PosRefAllele>
            >
        > MapType;

    FuzzyGenotypeMatcher(size_t numSamples, OutputWriter& out)
        : gtmap_(numSamples)
        , out_(out)
    {
    }

    void add(
            size_t sampleIdx,
            RawVariant::Vector const& alleles,
            size_t streamIdx,
            Vcf::Entry const* e)
    {
        for (auto iter = alleles.begin(); iter != alleles.end(); ++iter) {
            auto refPos = std::make_tuple(iter->pos, iter->ref);
            gtmap_[sampleIdx][refPos][iter->alt][streamIdx] = e;
        }
    }

    void report(std::string const& sequence) {
        /*
         *for (size_t sampleIdx = 0; sampleIdx < gtmap_.size(); ++sampleIdx) {
         *    auto& sd = gtmap_[sampleIdx];
         *    for (auto i = sd.begin(); i != sd.end(); ++i) {
         *        auto const& rawvs = i->first;
         *        auto const& who = i->second;
         *        out_(sampleIdx, sequence, rawvs, who);
         *    }
         *    sd.clear();
         *}
         */
    }

private:
    MapType gtmap_;
    OutputWriter& out_;
};

template<typename OutputWriter>
class GenotypeComparator {
public:
    template<typename HeaderVector>
    GenotypeComparator(
            std::vector<std::string> const& sampleNames,
            HeaderVector const& headers,
            size_t numStreams,
            OutputWriter& outputWriter
            )
        : sampleNames_(sampleNames)
        , sampleIndices_(numStreams)
        , numStreams_(numStreams)
        , entries_(numStreams)
        , final_(false)
        , collector_(sampleNames.size(), outputWriter)
    {
        for (auto name = sampleNames_.begin(); name != sampleNames_.end(); ++name) {
            for (size_t i = 0; i < headers.size(); ++i) {
                auto const& hdr = headers[i];
                sampleIndices_[i].push_back(hdr->sampleIndex(*name));
            }
        }
    }

    ~GenotypeComparator() {
        finalize();
    }

    void push(Entry* e) {
        if (final_) {
            throw std::runtime_error("Attempted to push entry onto finalized GenotypeComparator");
        }

        if (sequence_.empty()) {
            sequence_ = e->chrom();
            region_.begin = e->start();
        }
        else if (!want(e)) {
            process();
            region_.begin = e->start();
            if (sequence_ != e->chrom()) {
                sequence_ = e->chrom();
            }
        }

        size_t streamIdx = e->header().sourceIndex();
        entries_[streamIdx].push_back(e);
        region_.end = std::max(region_.end, e->stop());
    }

    void finalize() {
        if (!final_) {
            process();
        }
        final_ = true;
    }

private:
    bool want(Entry const* e) const {
        Region entryRegion = {e->start(), e->stop()};
        return sequence_ == e->chrom() && region_.overlap(entryRegion) > 0;
    }

    void processEntries(size_t streamIdx) {
        auto const& ents = entries_[streamIdx];
        for (auto e = ents.begin(); e != ents.end(); ++e) {
            if (e->isFiltered()) {
                continue;
            }

            auto const& sd = e->sampleData();
            RawVariant::Vector rawvs = RawVariant::processEntry(*e);
            if (rawvs.empty()) {
                continue;
            }

            for (size_t sampleIdx = 0; sampleIdx < sampleNames_.size(); ++sampleIdx) {
                RawVariant::Vector alleles;
                if (sd.isSampleFiltered(sampleIdx)) {
                    continue;
                }

                GenotypeCall const& call = sd.genotype(sampleIndices_[streamIdx][sampleIdx]);
                for (auto idx = call.indices().begin(); idx != call.indices().end(); ++idx) {
                    std::unique_ptr<RawVariant> rv;
                    if (*idx > 0) {
                        rv.reset(new RawVariant(rawvs[*idx - 1]));
                    }
                    else {
                        rv.reset(new RawVariant);
                        rv->pos = e->pos();
                        rv->alt = e->ref();
                        rv->ref = e->ref();
                    }
                    alleles.push_back(rv.release());
                }

                alleles.sort();
                if (!alleles.empty()) {
                    collector_.add(sampleIdx, alleles, streamIdx, &*e);
                }
            }
        }
    }

    void process() {
        for (size_t streamIdx = 0; streamIdx < entries_.size(); ++streamIdx) {
            processEntries(streamIdx);
        }

        collector_.report(sequence_);

        for (size_t streamIdx = 0; streamIdx < entries_.size(); ++streamIdx) {
            entries_[streamIdx].clear();
        }
    }

private:
    typedef boost::ptr_vector<Entry> EntryVector;

    std::vector<std::string> const& sampleNames_;
    std::vector<std::vector<size_t>> sampleIndices_;
    size_t numStreams_;
    Region region_;
    std::string sequence_;
    std::vector<EntryVector> entries_;
    bool final_;
    ExactGenotypeMatcher<OutputWriter> collector_;
};

template<typename HeaderVector, typename OutputWriter>
GenotypeComparator<OutputWriter> makeGenotypeComparator(
            std::vector<std::string> const& sampleNames,
            HeaderVector const& headers,
            size_t numStreams,
            OutputWriter& out
            )
{
    return GenotypeComparator<OutputWriter>(sampleNames, headers, numStreams, out);
}

END_NAMESPACE(Vcf)
