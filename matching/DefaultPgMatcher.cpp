#include "DefaultPgMatcher.h"

#include "../pseudogenome/DefaultPseudoGenome.h"
#include "../pseudogenome/PackedPseudoGenome.h"
#include "../pseudogenome/persistence/SeparatedPseudoGenomePersistence.h"
#include "../pseudogenome/persistence/SeparatedExtendedReadsList.h"
#include "ConstantLengthPatternsOnTextHashMatcher.h"
#include <list>

namespace PgTools {

    DefaultPgMatcher::DefaultPgMatcher(const string& srcPgPrefix, const string& targetPgPrefix, bool revComplMatching)
            :srcPgPrefix(srcPgPrefix), targetPgPrefix(targetPgPrefix), revComplMatching(revComplMatching) {
        destPgIsSrcPg = srcPgPrefix == targetPgPrefix;
        if (destPgIsSrcPg)
            cout << "Reading pseudogenome..." << endl;
        else
            cout << "Reading source pseudogenome..." << endl;
        PgTools::SeparatedPseudoGenomePersistence::getPseudoGenomeProperties(srcPgPrefix, srcPgh, plainTextReadMode);
        srcPg = PgTools::SeparatedPseudoGenomePersistence::getPseudoGenome(srcPgPrefix);
        cout << "Pseudogenome length: " << srcPgh->getPseudoGenomeLength() << endl;
        readLength = srcPgh->getMaxReadLength();

        if (targetPgPrefix != srcPgPrefix) {
            cout << "Reading target pseudogenome..." << endl;
            PseudoGenomeHeader *pgh = 0;
            bool plainTextReadMode = false;
            PgTools::SeparatedPseudoGenomePersistence::getPseudoGenomeProperties(targetPgPrefix, pgh,
                                                                                 plainTextReadMode);
            destPg = PgTools::SeparatedPseudoGenomePersistence::getPseudoGenome(targetPgPrefix);
            cout << "Pseudogenome length: " << pgh->getPseudoGenomeLength() << endl;
        } else
            destPg = srcPg;

        if (revComplMatching)
            destPg = PgSAHelpers::reverseComplement(destPg);
    }

    DefaultPgMatcher::~DefaultPgMatcher() {
        delete(srcPgh);
    }

    void backMatchExpand(const string& text, uint64_t& textPos, const string& pattern, uint64_t& patternPos, uint64_t& length) {
        const char* textGuardPtr = text.data();
        const char* textPtr = text.data() + textPos;
        const char* patternGuardPtr = pattern.data();
        const char* patternPtr = pattern.data() + patternPos;
        uint64_t i = 0;
        while (textPtr-- != textGuardPtr && patternPtr-- != patternGuardPtr && *textPtr == *patternPtr)
            i++;
        textPos -= i;
        patternPos -= i;
        length += i;
    }

    void forwardMatchExpand(const string& text, uint64_t textPos, const string& pattern, uint64_t patternPos, uint64_t& length) {
        const char* textGuardPtr = text.data() + text.length();
        const char* textPtr = text.data() + textPos + length;
        const char* patternGuardPtr = pattern.data() + pattern.length();
        const char* patternPtr = pattern.data() + patternPos + length;
        while (*textPtr == *patternPtr && textPtr++ != textGuardPtr && patternPtr++ != patternGuardPtr)
            length++;
    }

    void DefaultPgMatcher::exactMatchPg(uint32_t minMatchLength) {
        clock_checkpoint();

        cout << "Feeding pattern pseudogenome parts...\n" << endl;
        const uint_pg_len_max matchingLength = minMatchLength / 2;
        DefaultConstantLengthPatternsOnTextHashMatcher hashMatcher(matchingLength);

        const char* pgPtr = srcPg.data();
        uint32_t partsCount = srcPg.length() / matchingLength;
        for (uint32_t i = 0; i < partsCount; i++)
            hashMatcher.addPattern(pgPtr + i * matchingLength, i);

        cout << "... finished in " << clock_millis() << " msec. " << endl;
        clock_checkpoint();
        cout << "Matching...\n" << endl;

        const char* textPtr = destPg.data();
        hashMatcher.iterateOver(textPtr, destPg.length());

        pgMatches.clear();
        uint_pg_len_max furthestMatchEndPos = 0;
        list<PgMatch> currentMatches;

        int i = 0;
        uint64_t matchCount = 0;
        uint64_t matchCharsCount = 0;
        uint64_t matchCharsWithOverlapCount = 0;
        uint64_t shorterMatchCount = 0;
        uint64_t falseMatchCount = 0;
        while (hashMatcher.moveNext()) {
            uint64_t matchDestPos = hashMatcher.getHashMatchTextPosition();
            const uint32_t matchPatternIndex = hashMatcher.getHashMatchPatternIndex();
            uint64_t matchSrcPos = matchPatternIndex * matchingLength;
            if(destPgIsSrcPg && revComplMatching?destPg.length() - matchSrcPos < matchDestPos:matchDestPos >= matchSrcPos)
                continue;
            auto cmIt = currentMatches.begin();
            bool continueMatch = false;
            while (cmIt != currentMatches.end()) {
                if (matchDestPos + matchingLength > (*cmIt).endGrossPosDestPg())  {
                    currentMatches.erase(cmIt++);
                } else {
                    if ((*cmIt).posGrossSrcPg - (*cmIt).posGrossDestPg == matchSrcPos - matchDestPos) {
                        continueMatch = true;
                        break;
                    }
                    cmIt++;
                }
            }
            if (continueMatch)
                continue;

            bool confirmPatternMatch = strncmp(textPtr + matchDestPos, pgPtr + matchSrcPos, matchingLength) == 0;
            uint64_t matchLength = matchingLength;
            if (confirmPatternMatch) {
                backMatchExpand(destPg, matchDestPos, srcPg, matchSrcPos, matchLength);
                forwardMatchExpand(destPg, matchDestPos, srcPg, matchSrcPos, matchLength);
                if (matchLength >= minMatchLength) {
                    matchCount++;
                    matchCharsWithOverlapCount += matchLength;
                    matchCharsCount += matchLength - (matchDestPos < furthestMatchEndPos?
                                                      furthestMatchEndPos - matchDestPos:0);
                    const PgMatch &matchInfo = PgMatch(matchSrcPos, matchLength, matchDestPos);
                    if (furthestMatchEndPos < matchDestPos + matchLength)
                        furthestMatchEndPos = matchDestPos + matchLength;
                    pgMatches.push_back(matchInfo);
                    currentMatches.push_back(matchInfo);
                } else
                    shorterMatchCount++ ;
            } else
                falseMatchCount++;

/*            if ((i++ % 1000000) == 0 || matchLength > minMatchLength * 15) {
                 cout << (confirmPatternMatch?"Matched ":"False-matched ") << matchLength << " chars: <" << matchSrcPos << "; "
                     << (matchSrcPos + matchLength) << ") " << " in " << matchDestPos << " (" << matchCharsCount << " chars matched total)" << endl;
                 cout << "Elapsed time: " << ((double) destPg.length() / matchDestPos) * clock_millis() / 1000 << "[s]" << endl;
                if (matchLength < matchingLength * 10) {
                    uint8_t beforeChars = matchingLength / 4;
                    const uint64_t &minPos = min<uint64_t>(matchPatternIndex, matchDestPos);
                    beforeChars = minPos < beforeChars ? minPos : beforeChars;
                    uint8_t afterChars = matchingLength / 4;
                    const uint64_t &maxLength =
                            min<uint64_t>(srcPg.length() - matchSrcPos, destPg.length() - matchDestPos) -
                            matchLength;
                    afterChars = maxLength < afterChars ? maxLength : afterChars;
                    cout << srcPg.substr(matchSrcPos, matchLength) << endl;
                    cout << destPg.substr(matchDestPos, matchLength) << endl;
                    cout << srcPg.substr(matchSrcPos - beforeChars, beforeChars) << " ... " << srcPg.substr(matchSrcPos + matchLength, afterChars) << endl;
                    cout << destPg.substr(matchDestPos - beforeChars, beforeChars) << " ... " << destPg.substr(matchDestPos + matchLength, afterChars) << endl;
                }
            }*/
        }
        cout << "Finished matching in  " << clock_millis() << " msec. " << endl;
        cout << "Exact matched " << matchCount << " parts (too short matches: " << shorterMatchCount << "). False matches reported: " << falseMatchCount << "." << endl;
        cout << "Stage 1: Matched " << getTotalMatchStat(matchCharsCount) << " Pg characters. Sum length of all matches is " << matchCharsWithOverlapCount << "." << endl;

        std::sort(pgMatches.begin(), pgMatches.end(), [](const PgMatch& match1, const PgMatch& match2) -> bool
        { return match1.grossLength > match2.grossLength; });

        cout << "Largest matches:" << endl;
        for(uint32_t i = 0; i < pgMatches.size() && i < 10; i++)
            pgMatches[i].report(cout);
    }

    void DefaultPgMatcher::writeMatchesInfo(const string &dumpFilePrefix) {
        cout << "Sorry, unimplemented matches info dump feature." << endl;
    }

    using namespace PgTools;

    void DefaultPgMatcher::writeIntoPseudoGenome(const string &destPgFilePrefix) {
        srcRl = DefaultSeparatedExtendedReadsList::getConstantAccessExtendedReadsList(srcPgPrefix, srcPgh->getPseudoGenomeLength());

        if (revComplMatching)
            correctDestPositionDueToRevComplMatching();

        mapPgMatches2SrcReadsList();
        if (destPgIsSrcPg)
            reverseDestWithSrcForBetterMatchesMappingInTheSamePg();
        resolveMatchesOverlapInSrc();
        if (destPgIsSrcPg)
            resolveDestOverlapSrcConflictsInTheSamePg();

        transferMatchesFromSrcToDest(destPgFilePrefix);

        delete(srcRl);
    }

    void DefaultPgMatcher::mapPgMatches2SrcReadsList() {
        sort(pgMatches.begin(), pgMatches.end(), [this](const PgMatch& pgMatch1, const PgMatch& pgMatch2) -> bool
        { return pgMatch1.posGrossSrcPg < pgMatch2.posGrossSrcPg; });

        uint32_t cancelledMatchesCount = 0;
        uint_pg_len_max totalNetMatchCharsWithOverlapCount = 0;
        int64_t i = 0;
        for(PgMatch& pgMatch: pgMatches) {
            pgMatch.alignToReads(srcRl->pos, readLength, i);
            if (pgMatch.inactive(srcRl->pos, readLength))
                cancelledMatchesCount++;
            totalNetMatchCharsWithOverlapCount += pgMatch.netSrcLength(srcRl->pos, readLength);
        }
        cout << "Cancelled " << cancelledMatchesCount << " short matches due to external reads overlap." << endl;
        cout << "Stage 2: Net-matched " << getTotalMatchStat(totalNetMatchCharsWithOverlapCount) << " Pg src->dest sum length of all matches." << endl;
    }

    void DefaultPgMatcher::reverseDestWithSrcForBetterMatchesMappingInTheSamePg() {
        sort(pgMatches.begin(), pgMatches.end(), [this](const PgMatch& pgMatch1, const PgMatch& pgMatch2) -> bool
        { return pgMatch1.posGrossDestPg < pgMatch2.posGrossDestPg; });

        uint_pg_len_max totalNetReverseMatchCharsWithOverlapCount = 0;
        uint_pg_len_max totalNetMatchCharsWithOverlapCount = 0;
        uint32_t restoredMatchesCount = 0;
        int64_t i = 0;
        for (PgMatch& pgMatch: pgMatches) {
            PgMatch revMatch(pgMatch.posGrossDestPg, pgMatch.grossLength, pgMatch.posGrossSrcPg);
            revMatch.alignToReads(srcRl->pos, readLength, i);
            uint_pg_len_max revNetSrcLength = revMatch.netSrcLength(srcRl->pos, readLength);
            totalNetReverseMatchCharsWithOverlapCount += revNetSrcLength;
            if (revNetSrcLength > pgMatch.netSrcLength(srcRl->pos, readLength)) {
                if (pgMatch.netSrcLength(srcRl->pos, readLength) == 0)
                    restoredMatchesCount++;
                pgMatch = revMatch;
            }
            totalNetMatchCharsWithOverlapCount += pgMatch.netSrcLength(srcRl->pos, readLength);
        }

        cout << "Restored " << restoredMatchesCount << " short matches (cancelled earlier due to external reads overlap)." << endl;
        cout << "Stage 2a: Net-matched " << getTotalMatchStat(totalNetReverseMatchCharsWithOverlapCount) << " Pg dest->src sum length of all matches." << endl;
        cout << "Stage 2b: Optimized net-matched " << getTotalMatchStat(totalNetMatchCharsWithOverlapCount) << " Pg sum length of all matches." << endl;
    }

    void DefaultPgMatcher::resolveDestOverlapSrcConflictsInTheSamePg() {
        sort(pgMatches.begin(), pgMatches.end(), [this](const PgMatch& pgMatch1, const PgMatch& pgMatch2) -> bool
        { return pgMatch1.netEndPosSrcPg(srcRl->pos) < pgMatch2.netEndPosSrcPg(srcRl->pos); });

        vector<uint32_t> matchDestOrderedIdx(pgMatches.size());
        for(uint32_t i = 0; i < pgMatches.size(); i++)
            matchDestOrderedIdx[i] = i;
        sort(matchDestOrderedIdx.begin(), matchDestOrderedIdx.end(),
                [this](const uint32_t& pgMatchIdx1, const uint32_t& pgMatchIdx2) -> bool
            { return pgMatches[pgMatchIdx1].netPosDestPg(srcRl->pos, readLength, revComplMatching)
                < pgMatches[pgMatchIdx2].netPosDestPg(srcRl->pos,readLength, revComplMatching); });

        vector<uint_pg_len_max> maxNetEndPosDestPgUpTo(pgMatches.size());
        maxNetEndPosDestPgUpTo[0] = pgMatches[matchDestOrderedIdx[0]].netEndPosDestPg(srcRl->pos, readLength,
                revComplMatching);
        for(uint32_t i = 1; i < pgMatches.size(); i++) {
            maxNetEndPosDestPgUpTo[i] = pgMatches[matchDestOrderedIdx[i]].netEndPosDestPg(srcRl->pos, readLength,
                    revComplMatching);
            if (maxNetEndPosDestPgUpTo[i] < maxNetEndPosDestPgUpTo[i - 1])
                maxNetEndPosDestPgUpTo[i] = maxNetEndPosDestPgUpTo[i - 1];
        }

        uint_pg_len_max totalNetMatchCharsCount = 0;
        uint_pg_len_max collidedCharsCount = 0;
        uint32_t conflictsCount = 0;
        uint32_t resolvedConflictsCount = 0;
        int64_t dOrdIdx = 0;
        for (PgMatch& srcMatch: pgMatches) {
            totalNetMatchCharsCount += srcMatch.netSrcLength(srcRl->pos, readLength);
            while(srcMatch.netPosSrcPg(srcRl->pos, readLength) < maxNetEndPosDestPgUpTo[dOrdIdx] && --dOrdIdx > 0);
            while (++dOrdIdx < pgMatches.size()
                && pgMatches[matchDestOrderedIdx[dOrdIdx]].netPosDestPg(srcRl->pos, readLength, revComplMatching)
                    < srcMatch.netEndPosSrcPg(srcRl->pos)) {
                if (srcMatch.inactive(srcRl->pos, readLength))
                    break;
                PgMatch& destMatch = pgMatches[matchDestOrderedIdx[dOrdIdx]];
                if (!destMatch.inactive(srcRl->pos, readLength)
                    && destMatch.netEndPosDestPg(srcRl->pos, readLength, revComplMatching) > srcMatch.netPosSrcPg(srcRl->pos, readLength)) {
                    if (resolveDestSrcCollision(destMatch, srcMatch, collidedCharsCount))
                        resolvedConflictsCount++;
                    else
                        conflictsCount++;
                }
            }
        }
        totalNetMatchCharsCount -= collidedCharsCount;
        cout << conflictsCount << " src<->dest overlap conflicts result in cancelling matches (resolved " << resolvedConflictsCount << ")." << endl;
        cout << "Stage 3a: src<->dest conflicts filtered net-matched " << getTotalMatchStat(totalNetMatchCharsCount) << " Pg sum length of all matches." << endl;
    }

    bool DefaultPgMatcher::resolveDestSrcCollision(PgMatch &destMatch, PgMatch &srcMatch,
                                                   uint_pg_len_max &collidedCharsCount) {
        if (destMatch.netEndPosDestPg(srcRl->pos, readLength, revComplMatching) < srcMatch.netEndPosSrcPg(srcRl->pos) &&
            destMatch.netPosDestPg(srcRl->pos, readLength, revComplMatching) < srcMatch.netPosSrcPg(srcRl->pos, readLength)) {
            uint_pg_len_max overlapLength = destMatch.netEndPosDestPg(srcRl->pos, readLength, revComplMatching)
                    - srcMatch.netPosSrcPg(srcRl->pos, readLength);
            srcMatch.trimLeft(overlapLength, srcRl->pos, readLength);
            collidedCharsCount += overlapLength;
            return !srcMatch.inactive(srcRl->pos, readLength);
        }
        if (destMatch.netEndPosDestPg(srcRl->pos, readLength, revComplMatching) > srcMatch.netEndPosSrcPg(srcRl->pos) &&
            destMatch.netPosDestPg(srcRl->pos, readLength, revComplMatching) > srcMatch.netPosSrcPg(srcRl->pos, readLength)) {
            uint_pg_len_max overlapLength = srcMatch.netEndPosSrcPg(srcRl->pos)
                    - destMatch.netPosDestPg(srcRl->pos, readLength, revComplMatching);
            srcMatch.trimRight(overlapLength, srcRl->pos, readLength);
            collidedCharsCount += overlapLength;
            return !srcMatch.inactive(srcRl->pos, readLength);
        }
        if (destMatch.netSrcLength(srcRl->pos, readLength) > srcMatch.netSrcLength(srcRl->pos, readLength)) {
            collidedCharsCount += srcMatch.netSrcLength(srcRl->pos, readLength);
            srcMatch.endRlIdx = srcMatch.startRlIdx - 1;
        } else {
            if (&destMatch <= &srcMatch)
                collidedCharsCount += destMatch.netSrcLength(srcRl->pos, readLength);
            destMatch.endRlIdx = destMatch.startRlIdx - 1;
        }
        return false;
    }

    void DefaultPgMatcher::resolveMatchesOverlapInSrc() {
        sort(pgMatches.begin(), pgMatches.end(), [this](const PgMatch& pgMatch1, const PgMatch& pgMatch2) -> bool
        { return pgMatch1.netPosSrcPg(srcRl->pos, readLength) < pgMatch2.netPosSrcPg(srcRl->pos, readLength); });

        vector<uint_pg_len_max> maxNetEndPosSrcPgUpTo(pgMatches.size());
        maxNetEndPosSrcPgUpTo[0] = pgMatches[0].netEndPosSrcPg(srcRl->pos);
        for(uint32_t i = 1; i < pgMatches.size(); i++) {
            maxNetEndPosSrcPgUpTo[i] = pgMatches[i].netEndPosSrcPg(srcRl->pos);
            if (maxNetEndPosSrcPgUpTo[i] < maxNetEndPosSrcPgUpTo[i - 1])
                maxNetEndPosSrcPgUpTo[i] = maxNetEndPosSrcPgUpTo[i - 1];
        }

        uint_pg_len_max totalNetMatchCharsCount = 0;
        uint_pg_len_max collidedCharsCount = 0;
        uint32_t conflictsCount = 0;
        uint32_t resolvedConflictsCount = 0;
        int64_t lIdx = 0;
        for (PgMatch& rightMatch: pgMatches) {
            totalNetMatchCharsCount += rightMatch.netSrcLength(srcRl->pos, readLength);
            while(rightMatch.netPosSrcPg(srcRl->pos, readLength) < maxNetEndPosSrcPgUpTo[lIdx] && --lIdx > 0);
            while (++lIdx < pgMatches.size()
                   && pgMatches[lIdx].netPosSrcPg(srcRl->pos, readLength) < rightMatch.netEndPosSrcPg(srcRl->pos)) {
                if (rightMatch.inactive(srcRl->pos, readLength) || &pgMatches[lIdx] == &rightMatch)
                    break;
                if (!pgMatches[lIdx].inactive(srcRl->pos, readLength)
                    && pgMatches[lIdx].netEndPosSrcPg(srcRl->pos) > rightMatch.netPosSrcPg(srcRl->pos, readLength)) {
                    if (resolveSrcSrcCollision(pgMatches[lIdx], rightMatch, collidedCharsCount))
                        resolvedConflictsCount++;
                    else
                        conflictsCount++;
                }
            }
        }
        totalNetMatchCharsCount -= collidedCharsCount;
        cout << conflictsCount << " src<->src overlap conflicts result in cancelling matches (resolved " << resolvedConflictsCount << ")." << endl;
        cout << "Stage 3: All conflicts filtered net-matched " << getTotalMatchStat(totalNetMatchCharsCount) << " Pg sum length of all matches." << endl;
    }

    bool DefaultPgMatcher::resolveSrcSrcCollision(PgMatch &leftMatch, PgMatch &rightMatch,
                                                  uint_pg_len_max &collidedCharsCount) {
        if (leftMatch.netEndPosSrcPg(srcRl->pos) < rightMatch.netEndPosSrcPg(srcRl->pos) &&
            leftMatch.netPosSrcPg(srcRl->pos, readLength) < rightMatch.netPosSrcPg(srcRl->pos, readLength)) {
            uint_pg_len_max overlapLength = leftMatch.netEndPosSrcPg(srcRl->pos) - rightMatch.netPosSrcPg(srcRl->pos, readLength);
            rightMatch.trimLeft(overlapLength, srcRl->pos, readLength);
            collidedCharsCount += overlapLength;
            return !rightMatch.inactive(srcRl->pos, readLength);
        }
        if (leftMatch.netSrcLength(srcRl->pos, readLength) > rightMatch.netSrcLength(srcRl->pos, readLength)) {
            collidedCharsCount += rightMatch.netSrcLength(srcRl->pos, readLength);
            rightMatch.endRlIdx = rightMatch.startRlIdx - 1;
        } else {
            collidedCharsCount += leftMatch.netSrcLength(srcRl->pos, readLength);
            leftMatch.endRlIdx = leftMatch.startRlIdx - 1;
        }
        return false;
    }

    void DefaultPgMatcher::correctDestPositionDueToRevComplMatching() {
        for (PgMatch& pgMatch: pgMatches)
            pgMatch.posGrossDestPg = srcPgh->getPseudoGenomeLength() - (pgMatch.posGrossDestPg + pgMatch.grossLength);
    }

    string DefaultPgMatcher::getTotalMatchStat(uint_pg_len_max totalMatchLength) {
        return toString(totalMatchLength) + " (" + toString((totalMatchLength * 100.0) / srcPgh->getPseudoGenomeLength(), 1)+ "%)";
    }

    void DefaultPgMatcher::transferMatchesFromSrcToDest(const string &destPgPrefix) {
        sort(pgMatches.begin(), pgMatches.end(), [this](const PgMatch& pgMatch1, const PgMatch& pgMatch2) -> bool
        { return pgMatch1.netPosSrcPg(srcRl->pos, readLength) < pgMatch2.netPosSrcPg(srcRl->pos, readLength); });

        vector<uint32_t> matchDestOrderedIdx;
        if (destPgIsSrcPg) {
            matchDestOrderedIdx.resize(pgMatches.size());
            for (uint32_t i = 0; i < pgMatches.size(); i++)
                matchDestOrderedIdx[i] = i;
            sort(matchDestOrderedIdx.begin(), matchDestOrderedIdx.end(),
                 [this](const uint32_t &pgMatchIdx1, const uint32_t &pgMatchIdx2) -> bool {
                     return pgMatches[pgMatchIdx1].netPosDestPg(srcRl->pos, readLength, revComplMatching)
                     < pgMatches[pgMatchIdx2].netPosDestPg(srcRl->pos, readLength, revComplMatching);
                 });
        }
        string newSrcPg;
        vector<bool> isReadRemapped(srcPgh->getReadsCount() + 1, false);
        newRlPos.resize(srcPgh->getReadsCount() + 1);

        uint64_t dOrdIdx = 0;
        uint_pg_len_max pos = 0;
        uint_pg_len_max lastReadPos = 0;
        uint_reads_cnt_max i = 0;
        uint_pg_len_max removedCount = 0;
        for (PgMatch& pgMatch: pgMatches) {
            if (pgMatch.inactive(srcRl->pos, readLength))
                continue;
            if (i >= pgMatch.startRlIdx) {
                removedCount += pgMatch.netEndPosSrcPg(srcRl->pos) - pos;
            } else {
                uint_read_len_max offset = srcRl->pos[i] - removedCount - lastReadPos;
                newSrcPg.append(srcPg, pos, pgMatch.netPosSrcPg(srcRl->pos, readLength) - pos);
                while (i < pgMatch.startRlIdx) {
                    newRlPos[i] = srcRl->pos[i] - removedCount;
                    lastReadPos = newRlPos[i++];
                }
                if (destPgIsSrcPg) {
                    while (dOrdIdx < pgMatches.size() &&
                           pgMatches[matchDestOrderedIdx[dOrdIdx]].netPosDestPg(srcRl->pos, readLength, revComplMatching) <
                           pgMatch.netPosSrcPg(srcRl->pos, readLength)) {
                        PgMatch &destMatch = pgMatches[matchDestOrderedIdx[dOrdIdx++]];
                        if (!destMatch.inactive(srcRl->pos, readLength))
                            destMatch.posGrossDestPg -= removedCount;
                    }
                }
                removedCount += pgMatch.netSrcLength(srcRl->pos, readLength);
            }
            pos = pgMatch.netEndPosSrcPg(srcRl->pos);
            i = pgMatch.endRlIdx + 1;
        }
        newSrcPg.append(srcPg, pos, srcPg.length() - pos);
        cout << "Source Pg reduced to " << newSrcPg.length() << " symbols (removed: " <<
            getTotalMatchStat(srcPgh->getPseudoGenomeLength() - newSrcPg.length()) << ")." << endl;
        if (destPgIsSrcPg)
            while (dOrdIdx < pgMatches.size()) {
                PgMatch& destMatch = pgMatches[matchDestOrderedIdx[dOrdIdx++]];
                if (!destMatch.inactive(srcRl->pos, readLength))
                    destMatch.posGrossDestPg -= removedCount;
            }

        for (PgMatch& pgMatch: pgMatches) {
            if (pgMatch.inactive(srcRl->pos, readLength))
                continue;
            for(uint_reads_cnt_max i = pgMatch.startRlIdx; i <= pgMatch.endRlIdx; i++) {
                isReadRemapped[i] = true;
                newRlPos[i] = pgMatch.mapSrcReadToDest(srcRl->pos[i], readLength, revComplMatching);
            }
        }
        srcRl->pos.clear();

        vector<uint_reads_cnt_max> rlPosOrd(srcPgh->getReadsCount());
        for (uint_reads_cnt_max i = 0; i < srcPgh->getReadsCount(); i++)
            rlPosOrd[i] = i;
        sort(rlPosOrd.begin(), rlPosOrd.end(),
             [this](const uint_reads_cnt_max &rlPosIdx1, const uint_reads_cnt_max &rlPosIdx2) -> bool {
                 return newRlPos[rlPosIdx1] < newRlPos[rlPosIdx2];
             });

        SeparatedPseudoGenomeOutputBuilder builder(destPgIsSrcPg?destPgPrefix:srcPgPrefix);
        builder.copyPseudoGenomeHeader(srcPgPrefix);

        pos = 0;
        removedCount = 0;
        uint_pg_len_max totalOffsetOverflow = 0;
        DefaultReadsListEntry entry;
        for(uint_reads_cnt_max iOrd = 0; iOrd < srcPgh->getReadsCount(); iOrd++) {
            uint_reads_cnt_max i = rlPosOrd[iOrd];
            if(destPgIsSrcPg || !isReadRemapped[i]) {
                entry.advanceEntryByPosition(newRlPos[i] - removedCount, srcRl->orgIdx[i],
                        srcRl->revComp[i] != (destPgIsSrcPg?(revComplMatching && isReadRemapped[i]):false));
                srcRl->copyMismatchesToEntry(i, entry);
                if (entry.offset > readLength) {
                    uint_pg_len_max overflow = entry.offset - readLength;
                    totalOffsetOverflow += overflow;
                    removedCount += overflow;
                    entry.offset = readLength;
                    entry.pos -= overflow;
                    builder.appendPseudoGenome(newSrcPg.substr(pos, newRlPos[i] - overflow - pos));
                    pos = newRlPos[i];
                }
                builder.writeExtraReadEntry(entry);
            }
        }
        builder.appendPseudoGenome(newSrcPg.substr(pos, newSrcPg.length() - pos));
        cout << "Final size of Pg: " << (newSrcPg.length() - totalOffsetOverflow) << " (removed "
            << totalOffsetOverflow << " chars in overflowed offsets)" << endl;
        builder.build();

        if(!destPgIsSrcPg) {
            cout << "Error: Unimplemented adding reads removed from source Pg to destination Pg.";
            exit(EXIT_FAILURE);
        }
        newRlPos.clear();
    }

    void matchPgInPgFile(const string &srcPgPrefix, const string &targetPgPrefix, uint_pg_len_max targetMatchLength,
                         const string &destPgPrefix, bool revComplPg, bool dumpInfo) {

        PgTools::DefaultPgMatcher matcher(srcPgPrefix, targetPgPrefix, revComplPg);

        matcher.exactMatchPg(targetMatchLength);

        if (dumpInfo)
            matcher.writeMatchesInfo(destPgPrefix);

        matcher.writeIntoPseudoGenome(destPgPrefix);
    }
}

