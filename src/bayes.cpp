// 
// BamBayes
//
// A bayesian genetic variant caller.
// 

// standard includes
//#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <time.h>

// "boost" regular expression library
#include <boost/regex.hpp>

// "boost" string manipulation
#include <boost/algorithm/string/join.hpp>

// "hash_map" true hashes
#include <ext/hash_map>

// private libraries
#include "Class-GigReader.h"
#include "Function-Sequence.h"
#include "Function-Generic.h"
#include "Function-Math.h"
#include "Class-BedReader.h"
#include "Class-FastaReader.h"
#include "BamReader.h"
#include "ReferenceSequenceReader.h"
#include "Fasta.h"
#include "TryCatch.h"
#include "Parameters.h"
#include "Allele.h"
#include "Caller.h"

#include "multichoose.h"

using namespace std; 

int main (int argc, char *argv[]) {

    Caller* caller = new Caller(argc, argv);
    list<Allele> alleles;

    ///////////////////////////////////////////
    // for each region in region list
    //     for each position in region
    //         for each read overlapping position
    //             for each base in read
    //                 register base


    while (caller->getNextAlleles(alleles)) {
        if (alleles.size() == 0)
            continue;
        int refallelecount = 0;
        int snpallelecount = 0;
        vector<Allele*> allelepts;
        for (list<Allele>::iterator it = alleles.begin(); it != alleles.end(); ++it) {
            Allele a = *it;
            if (a.type == ALLELE_SNP) {
                ++snpallelecount;
            }
            if (a.type == ALLELE_REFERENCE)
                ++refallelecount;
            allelepts.push_back(&*it);
        }

        vector<vector<Allele> > alleleGroups = groupAlleles(alleles, allelesEquivalent);
        vector<vector<Allele> > sampleGroups = groupAlleles(alleles, allelesSameSample);
        vector<Allele> genotypeAlleles = genotypeAllelesFromAlleleGroups(alleleGroups);
        vector<vector<Allele> > genotypeCombos = multichoose(2, genotypeAlleles);

        // log our progress
        cout << "sequence " << caller->currentTarget->seq
            << " position " << caller->currentPosition 
            << " counting "
            << refallelecount << " reference alleles, " 
            << snpallelecount << " snp alleles, "
            << genotypeAlleles.size() << " genotype alleles, and "
            << genotypeCombos.size() << " genotype combinations" << endl;

        cout << genotypeCombos.size() << " genotypes: " << endl;
        for (vector<vector< Allele > >::iterator genotype = genotypeCombos.begin();
                genotype != genotypeCombos.end(); genotype ++) {
            cout << *genotype << endl;
        }
        cout << endl;

        vector<vector<double> > probsBySample;

        for (vector<vector< Allele > >::iterator sampleAlleles = sampleGroups.begin();
                sampleAlleles != sampleGroups.end(); sampleAlleles++) {
            cout << *sampleAlleles << endl;
            vector<double> probs = caller->probObservedAllelesGivenGenotypes(*sampleAlleles, genotypeCombos);
            probsBySample.push_back(probs);
            int i = 0;
            for (vector<vector< Allele > >::iterator genotype = genotypeCombos.begin(); 
                    genotype != genotypeCombos.end(); genotype++) {
                cout << "{ " << *genotype << " } : ";
                cout << "\t" << probs[i] << endl;
                ++i;
            }
            cout << endl;
        }

        // broken
        //double normalizer = bayesianNormalizationFactor(genotypeCombos, probsBySample, sampleGroups);
        //double approximatenormalizer = approximateBayesianNormalizationFactor(genotypeCombos, probsBySample, sampleGroups);
        //cout << "posterior normalizer: " << normalizer << endl;
        //cout << "approximate posterior normalizer: " << approximatenormalizer << endl;

        // output most-likely genotype vector for all individuals
        
        vector<pair<double, vector<Allele> > > mlgt = mostLikelyGenotypesGivenObservations(genotypeCombos, probsBySample, true);

        double probProduct = 1;
        int i = 0;
        for (vector<vector<Allele> >::iterator s = sampleGroups.begin();
                s != sampleGroups.end(); s++) {
            probProduct *= mlgt.at(i).first;
            cout << "best genotype: "<< s->front().sampleID << " " << mlgt.at(i).second <<  " " <<  mlgt.at(i).first << " " << s->size() << endl;
            i++;
        }

        //cout << "probability of best genotype vector: " << probProduct / normalizer << endl << endl;

    }

    delete caller;

    return 0;

}

// discrete elements of analysis
// 
// 1) fasta reference
// 2) bam file(s) over samples / samples
// 3) per-individual base calls (incl cigar)
// 4) priors
// 
// sets of data per individual
// and sets of data per position
// 
// for each position in the target regions (which is provided by a bed file)
// calculate the basecalls for each sample
// then, for samples for which we meet certain criteria (filters):
//     number of mismatches
//     data sufficiency (number of individual basecalls aka reads?)
//     (readmask ?)
// ... establish the probability of a snp for each possible genotype
// (which is ~ the data likelihood * the prior probablity of a snp for each sample)
// and report the top (configurable) number of possible genotypes
// 
// 
// 
// high level overview of progression:
// 
// for each region in region list
//     for each position in region
//         for each read overlapping position
//             for each base in read
//                 register base
//         evaluate prob(variation | all bases)
//         if prob(variation) >= reporting threshold
//             report variation
// 
// 
// registration of bases:
// 
//     skip clips (soft and hard)
//     presently, skip indels and only analyze aligned bases, but open development
//         to working with them in the future
//     preadmask: when we encounter a indel alignment, mask out bases in that read
//         because we are concerned about the semantics of processing it.
//     
//     we keep data on the bases, the basecall struct contains this information
// 
// 
// probability estimation
// 
//     p ( snp ) ~= ...
// 
//     p ( individual genotype | reads ) ~= ...
// 
//     p ( genotypes | basecalls ) ~= p( basecalls | genotype ) * prior( genotype ) / probability ( basecalls )
// 
// 
// 
// algorithmic core overview:
// 
// (1) individual data likelihoods
// 
// for each sample in the sample list
//     get basecalls corresponding to sample
//         for each genotype from the fixed genotype list
//             calculate the data likelihoods of p ( basecalls | genotype )   == "data likelihood" 
//                  this amounts to multiplying the quality scores from all the basecalls in that sample
// 
// (2) total genotype likelhoods for dominant genotype combinations
// 
// for each genotype combo in dominant genotype combo list
//     data likelhood p ( basecall combo | genotype combo )
// 
// 
// (3) calculate priors for dominant genotype combinations
// 
// for each genotype combo in dominant genotype combo list
//     calculate priors of that genotype combo  (well defined)
// 
// (4) calculate posterior probability of dominant genotype combinations
// 
// for each genotype combo in dominant genotype combo list
//     multiply results of corresponding (2) * (3)
// normalize  (could be a separate step)
// 
// (5) probability that of a variation given all basecalls
// 
// sum over probability of all dominant variants
// 
// (6) calculate individual sample genotype posterior marginals
// 
// for each sample
//     for each genotype
//         sum of p(genotype | reads) for fixed genotype <-- (4)