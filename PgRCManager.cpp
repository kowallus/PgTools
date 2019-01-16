#include "PgRCManager.h"

#include "matching/ReadsMatchers.h"
#include "matching/SimplePgMatcher.h"
#include "pseudogenome/TemplateUserGenerator.h"
#include "readsset/persistance/ReadsSetPersistence.h"
#include "readsset/tools/division.h"
#include "pseudogenome/generator/GreedySwipingPackedOverlapPseudoGenomeGenerator.h"
#include "pseudogenome/persistence/PseudoGenomePersistence.h"
#include "pseudogenome/persistence/SeparatedPseudoGenomePersistence.h"

namespace PgTools {

    static const char *const BAD_INFIX = "_bad";
    static const char *const GOOD_INFIX = "_good";
    static const char *const N_INFIX = "_N";
    static const char *const DIVISION_EXTENSION = ".div";

    uint_read_len_max probeReadsLength(const string &srcFastqFile);
    clock_t getTimeInSec(clock_t end_t, clock_t begin_t) { return ((end_t - begin_t) / CLOCKS_PER_SEC); }


    void PgRCManager::prepareChainData() {
        qualityDivision = error_limit_in_promils < 1000;
        readsLength = probeReadsLength(srcFastqFile);
        targetMismatches = readsLength / targetCharsPerMismatch;
        maxMismatches = readsLength / maxCharsPerMismatch;

        if (qualityDivision)
            pgFilesPrefixes = pgFilesPrefixes + "_q" + toString(error_limit_in_promils);
        pgFilesPrefixes = pgFilesPrefixes + (filterNReads2Bad?"_N":"") + "_g" + gen_quality_str;
        badDivisionFile = pgFilesPrefixes + BAD_INFIX + DIVISION_EXTENSION;
        pgGoodPrefix = pgFilesPrefixes + GOOD_INFIX;
        pgFilesPrefixesWithM = pgFilesPrefixes + "_m" + toString(targetCharsPerMismatch)
                                      + "_M" + mismatchesMode + toString(maxCharsPerMismatch) + "_p" + toString(minimalPgMatchLength);
        pgMappedGoodPrefix = pgFilesPrefixesWithM + GOOD_INFIX;
        pgMappedBadPrefix = pgFilesPrefixesWithM + BAD_INFIX;
        pgNPrefix = pgFilesPrefixesWithM + N_INFIX;
        mappedBadDivisionFile = pgFilesPrefixesWithM + DIVISION_EXTENSION;
        if (skipIntermediateOutput) {
            pgGoodPrefix = pgMappedGoodPrefix;
            badDivisionFile = mappedBadDivisionFile;
        }
    }

    void PgRCManager::executePgRCChain() {
        start_t = clock();

        prepareChainData();

        uint8_t stageCount = 0;
        if (skipStages < ++stageCount && qualityDivision) {
            ReadsSourceIteratorTemplate<uint_read_len_max> *allReadsIterator = new FASTQReadsSourceIterator<uint_read_len_max>(
                    srcFastqFile, pairFastqFile);
            divideReads(allReadsIterator, badDivisionFile, error_limit_in_promils / 1000.0, filterNReads2Bad);
            delete (allReadsIterator);
        }
        div_t = clock();
        if (skipStages < ++stageCount && endAtStage >= stageCount) {
            ReadsSourceIteratorTemplate<uint_read_len_max> *goodReadsIterator;
            if (qualityDivision) goodReadsIterator = ReadsSetPersistence::createManagedReadsIterator(
                        srcFastqFile, pairFastqFile, badDivisionFile, true, revComplPairFile, false, false);
            else goodReadsIterator = new FASTQReadsSourceIterator<uint_read_len_max>(
                        srcFastqFile, pairFastqFile);
            const vector<bool> &badReads = GreedySwipingPackedOverlapPseudoGenomeGeneratorFactory::getBetterReads(
                    goodReadsIterator, gen_quality_coef);
            IndexesMapping* goodIndexesMapping = goodReadsIterator->retainVisitedIndexesMapping();
            delete (goodReadsIterator);
            ReadsSetPersistence::writeOutputDivision(goodIndexesMapping, badReads,
                                                     true, badDivisionFile, true);

            delete(goodIndexesMapping);
        }
        pgDiv_t = clock();
        if (skipStages < ++stageCount && endAtStage >= stageCount) {
            ReadsSourceIteratorTemplate<uint_read_len_max> *goodReadsIterator = ReadsSetPersistence::createManagedReadsIterator(
                    srcFastqFile, pairFastqFile, badDivisionFile, true, revComplPairFile, false, false);
            PseudoGenomeBase *goodPgb = GreedySwipingPackedOverlapPseudoGenomeGeneratorFactory::generatePg(
                    goodReadsIterator);
            IndexesMapping* good2IndexesMapping = goodReadsIterator->retainVisitedIndexesMapping();
            delete (goodReadsIterator);
            SeparatedPseudoGenomePersistence::writePseudoGenome(goodPgb, pgGoodPrefix, good2IndexesMapping,
                                                                revComplPairFile);
            delete (goodPgb);
            delete(good2IndexesMapping);
        }
        good_t = clock();
        if (skipStages < ++stageCount && endAtStage >= stageCount) {
            ReadsSourceIteratorTemplate<uint_read_len_max> *badReadsIterator = ReadsSetPersistence::createManagedReadsIterator(
                    srcFastqFile, pairFastqFile, badDivisionFile, false);
            cout << "Reading (div: " << badDivisionFile << ") reads set\n";
            PackedConstantLengthReadsSet *badReadsSet = PackedConstantLengthReadsSet::loadReadsSet(badReadsIterator);
            badReadsSet->printout();
            IndexesMapping* badIndexesMapping = badReadsIterator->retainVisitedIndexesMapping();
            delete (badReadsIterator);
            mapReadsIntoPg(
                    pgGoodPrefix, true, badReadsSet, DefaultReadsMatcher::DISABLED_PREFIX_MODE,
                    targetMismatches, maxMismatches, mismatchesMode, 0,
                    false, pgMappedGoodPrefix, badIndexesMapping, false, mappedBadDivisionFile);

            delete (badReadsSet);
            delete(badIndexesMapping);
        }
        match_t = clock();
        if (skipStages < ++stageCount && endAtStage >= stageCount) {
            {
                ReadsSourceIteratorTemplate<uint_read_len_max> *mappedBadReadsIterator = ReadsSetPersistence::createManagedReadsIterator(
                        srcFastqFile, pairFastqFile, mappedBadDivisionFile, false, revComplPairFile, ignoreNReads, false);
                PseudoGenomeBase *badPgb = GreedySwipingPackedOverlapPseudoGenomeGeneratorFactory::generatePg(
                        mappedBadReadsIterator);
                IndexesMapping* mappedBadIndexesMapping = mappedBadReadsIterator->retainVisitedIndexesMapping();
                delete (mappedBadReadsIterator);
                SeparatedPseudoGenomePersistence::writePseudoGenome(badPgb, pgMappedBadPrefix, mappedBadIndexesMapping,
                                                                    revComplPairFile);
                delete (badPgb);
                delete(mappedBadIndexesMapping);
            }
            if (ignoreNReads) {
                ReadsSourceIteratorTemplate<uint_read_len_max> *mappedNReadsIterator = ReadsSetPersistence::createManagedReadsIterator(
                        srcFastqFile, pairFastqFile, mappedBadDivisionFile, false, revComplPairFile, false, true);
                PseudoGenomeBase *nPgb = GreedySwipingPackedOverlapPseudoGenomeGeneratorFactory::generatePg(
                        mappedNReadsIterator);
                IndexesMapping* mappedNIndexesMapping = mappedNReadsIterator->retainVisitedIndexesMapping();
                delete (mappedNReadsIterator);
                SeparatedPseudoGenomePersistence::writePseudoGenome(nPgb, pgNPrefix, mappedNIndexesMapping,
                                                                    revComplPairFile);
                delete (nPgb);
                delete(mappedNIndexesMapping);
            }
        }
        bad_t = clock();
        if (skipStages < ++stageCount && endAtStage >= stageCount) {
            //DefaultPgMatcher::matchPgInPgFile(pgMappedGoodPrefix, pgMappedGoodPrefix, readsLength, pgGoodPrefix, true, false);
            SimplePgMatcher::matchPgInPgFiles(pgMappedGoodPrefix, pgMappedBadPrefix, minimalPgMatchLength, true);
        }
        gooder_t = clock();
        if (pairFastqFile != "" && skipStages < ++stageCount && endAtStage >= stageCount) {
            if (ignoreNReads)
                SeparatedPseudoGenomePersistence::dumpPgPairs({pgMappedGoodPrefix, pgMappedBadPrefix, pgNPrefix});
            else
                SeparatedPseudoGenomePersistence::dumpPgPairs({pgMappedGoodPrefix, pgMappedBadPrefix});
        }

        reportTimes();
    }

    void PgRCManager::reportTimes() {
        string outputfile = "pgrc_res.txt";
        bool hasHeader = (bool) std::ifstream(outputfile);
        fstream fout(outputfile, ios::out | ios::binary | ios::app);
        if (!hasHeader)
            fout << "srcFastq\tpairFastq\trcPairFile\tpgPrefix\tq[%o]\tg[%o]\tm\tM\tp\ttotal[s]\tdiv[s]\tPgDiv[s]\tgood[s]\treadsMatch[s]\tbad[s]\tpgMatch[s]\tpost[s]" << endl;

        fout << srcFastqFile << "\t" << pairFastqFile << "\t" << (revComplPairFile?"yes":"no") << "\t"
             << pgFilesPrefixes << "\t" << toString(error_limit_in_promils) << "\t" << gen_quality_str << "\t"
             << (int) targetMismatches << "\t" << mismatchesMode << (int) maxMismatches << "\t" << minimalPgMatchLength << "\t";
        fout << getTimeInSec(clock(), start_t) << "\t";
        fout << getTimeInSec(div_t, start_t) << "\t";
        fout << getTimeInSec(pgDiv_t, div_t) << "\t";
        fout << getTimeInSec(good_t, pgDiv_t) << "\t";
        fout << getTimeInSec(match_t, good_t) << "\t";
        fout << getTimeInSec(bad_t, match_t) << "\t";
        fout << getTimeInSec(gooder_t, bad_t) << "\t";
        fout << getTimeInSec(clock(), gooder_t) << endl;
    }

    uint_read_len_max probeReadsLength(const string &srcFastqFile) {
        ReadsSourceIteratorTemplate<uint_read_len_max> *readsIt = ReadsSetPersistence::createManagedReadsIterator(
                srcFastqFile);
        readsIt->moveNext();
        uint_read_len_max readsLength = readsIt->getReadLength();
        delete(readsIt);
        return readsLength;
    }


}