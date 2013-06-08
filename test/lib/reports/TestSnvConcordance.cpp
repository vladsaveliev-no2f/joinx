#include "reports/SnvConcordance.hpp"
#include "common/Sequence.hpp"
#include "fileformats/Variant.hpp"
#include "fileformats/Bed.hpp"

#include <gtest/gtest.h>
#include <string>

using namespace std;

namespace {
    Variant makeVariant(string chrom, int64_t start, int64_t stop, string refCall) {
        Bed::ExtraFieldsType extra;
        extra.push_back(refCall);
        Bed b(chrom, start,stop, extra);
        return Variant(b);
    }
}

TEST(TestSnvConcordance, overlap) {
    ASSERT_EQ(0u, SnvConcordance::overlap("A", "C"));
    ASSERT_EQ(0u, SnvConcordance::overlap("A", "G"));
    ASSERT_EQ(0u, SnvConcordance::overlap("A", "T"));
    ASSERT_EQ(0u, SnvConcordance::overlap("T", "A"));
    ASSERT_EQ(1u, SnvConcordance::overlap("A", "A"));
    ASSERT_EQ(1u, SnvConcordance::overlap("A", "AC"));
    ASSERT_EQ(1u, SnvConcordance::overlap("A", "ACGT"));
    ASSERT_EQ(2u, SnvConcordance::overlap("AG", "ACG"));
    ASSERT_EQ(2u, SnvConcordance::overlap("AT", "ACGT"));
    ASSERT_EQ(2u, SnvConcordance::overlap("AC", "ACGT"));
    ASSERT_EQ(3u, SnvConcordance::overlap("ACG", "ACGT"));
    ASSERT_EQ(4u, SnvConcordance::overlap("ACGT", "ACGT"));
}

TEST(TestSnvConcordance, describeSnv) {
    SnvDescription d;

    d = SnvConcordance::describeSnv("A", "N");
    ASSERT_EQ(AMBIGUOUS, d.type);
    ASSERT_EQ(HOMOZYGOUS, d.zygosity.type);
    ASSERT_EQ(1u, d.zygosity.alleleCount);
    d = SnvConcordance::describeSnv("N", "N");
    ASSERT_EQ(AMBIGUOUS, d.type);
    ASSERT_EQ(HOMOZYGOUS, d.zygosity.type);
    ASSERT_EQ(1u, d.zygosity.alleleCount);
    d = SnvConcordance::describeSnv("C", "C");
    ASSERT_EQ(REFERENCE, d.type);
    ASSERT_EQ(HOMOZYGOUS, d.zygosity.type);
    ASSERT_EQ(1u, d.zygosity.alleleCount);
    d = SnvConcordance::describeSnv("C", "G");
    ASSERT_EQ(SNV, d.type);
    ASSERT_EQ(HOMOZYGOUS, d.zygosity.type);
    ASSERT_EQ(1u, d.zygosity.alleleCount);

    d = SnvConcordance::describeSnv("C", "CG");
    ASSERT_EQ(SNV, d.type);
    ASSERT_EQ(HETEROZYGOUS, d.zygosity.type);
    ASSERT_EQ(2u, d.zygosity.alleleCount);
    ASSERT_EQ(1u, d.overlap);

    d = SnvConcordance::describeSnv("C", "CGT");
    ASSERT_EQ(SNV, d.type);
    ASSERT_EQ(HETEROZYGOUS, d.zygosity.type);
    ASSERT_EQ(3u, d.zygosity.alleleCount);
    ASSERT_EQ(1u, d.overlap);

    d = SnvConcordance::describeSnv("CG", "CGT");
    ASSERT_EQ(SNV, d.type);
    ASSERT_EQ(HETEROZYGOUS, d.zygosity.type);
    ASSERT_EQ(3u, d.zygosity.alleleCount);
    ASSERT_EQ(2u, d.overlap);
}

TEST(TestSnvConcordance, snvDescription) {
    Variant a = makeVariant("1", 2, 3, "A/T");
    Variant b = makeVariant("1", 2, 3, "A/T");
    MatchDescription d = SnvConcordance::matchDescription(a, b);
    ASSERT_EQ(MATCH, d.matchType);

    b = makeVariant("1", 2, 3, "A/C");
    d = SnvConcordance::matchDescription(a, b);
    ASSERT_EQ(MISMATCH, d.matchType);

    b = makeVariant("1", 2, 3, "C/C");
    d = SnvConcordance::matchDescription(a, b);
    ASSERT_EQ(REFERENCE_MISMATCH, d.matchType);

    b = makeVariant("1", 2, 3, "A/Y");
    d = SnvConcordance::matchDescription(a, b);
    ASSERT_EQ(PARTIAL_MATCH, d.matchType);
}

TEST(TestSnvConcordance, snvDescriptionString) {
    SnvDescription v;
    v.zygosity.type = HOMOZYGOUS;
    v.type = AMBIGUOUS;
    ASSERT_EQ("homozygous ambiguous call", v.toString());

    v.zygosity.alleleCount = 1;
    v.type = REFERENCE;
    ASSERT_EQ("homozygous reference", v.toString());

    v.type = SNV;
    ASSERT_EQ("homozygous snv", v.toString());

    v.zygosity.type = HETEROZYGOUS;
    v.zygosity.alleleCount = 3;
    v.overlap = 1;
    ASSERT_EQ("heterozygous (2 alleles) snv", v.toString());
}
