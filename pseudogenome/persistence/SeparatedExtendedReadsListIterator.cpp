#include "SeparatedExtendedReadsListIterator.h"

namespace PgTools {

    SeparatedExtendedReadsListIterator::SeparatedExtendedReadsListIterator(const string &pseudoGenomePrefix)
            : pseudoGenomePrefix(pseudoGenomePrefix) {
        SeparatedPseudoGenomePersistence::getPseudoGenomePropertes(pseudoGenomePrefix, pgh, plainTextReadMode);
        initSrcs();
    }

    SeparatedExtendedReadsListIterator::~SeparatedExtendedReadsListIterator() {
        delete(pgh);
        freeSrcs();
    }

    void SeparatedExtendedReadsListIterator::initSrc(ifstream *&src, const string &fileSuffix) {
        src = new ifstream(pseudoGenomePrefix + fileSuffix, ios_base::in | ios_base::binary);
        if (src->fail()) {
            delete (src);
            src = 0;
        }
    }

    void SeparatedExtendedReadsListIterator::initSrcs() {
        initSrc(rlPosSrc, SeparatedPseudoGenomePersistence::READSLIST_POSITIONS_FILE_SUFFIX);
        initSrc(rlOrgIdxSrc, SeparatedPseudoGenomePersistence::READSLIST_ORIGINAL_INDEXES_FILE_SUFFIX);
        initSrc(rlRevCompSrc, SeparatedPseudoGenomePersistence::READSLIST_REVERSECOMPL_FILE_SUFFIX);
        initSrc(rlMisCntSrc, SeparatedPseudoGenomePersistence::READSLIST_MISMATCHES_COUNT_FILE_SUFFIX);
        initSrc(rlMisSymSrc, SeparatedPseudoGenomePersistence::READSLIST_MISMATCHED_SYMBOLS_FILE_SUFFIX);
        initSrc(rlMisOffSrc, SeparatedPseudoGenomePersistence::READSLIST_MISMATCHES_POSITIONS_FILE_SUFFIX);

        initSrc(rlOffSrc, SeparatedPseudoGenomePersistence::READSLIST_OFFSETS_FILE_SUFFIX);
        initSrc(rlMisRevOffSrc, SeparatedPseudoGenomePersistence::READSLIST_MISMATCHES_REVOFFSETS_FILE_SUFFIX);
    }

    void SeparatedExtendedReadsListIterator::freeSrc(ifstream *&src) {
        if (src) {
            src->close();
            delete (src);
            src = 0;
        }
    }

    void SeparatedExtendedReadsListIterator::freeSrcs() {
        freeSrc(rlPosSrc);
        freeSrc(rlOrgIdxSrc);
        freeSrc(rlRevCompSrc);
        freeSrc(rlMisCntSrc);
        freeSrc(rlMisSymSrc);
        freeSrc(rlMisOffSrc);

        freeSrc(rlOffSrc);
        freeSrc(rlMisRevOffSrc);
    }

    bool SeparatedExtendedReadsListIterator::moveNext() {
        if (++current < pgh->getReadsCount()) {
            uint_reads_cnt_std idx;
            uint8_t revComp = 0;
            PgSAHelpers::readValue<uint_reads_cnt_std>(*rlOrgIdxSrc, idx, plainTextReadMode);
            if (rlRevCompSrc)
                PgSAHelpers::readValue<uint8_t>(*rlRevCompSrc, revComp, plainTextReadMode);
            if (rlOffSrc) {
                uint_read_len_max offset;
                PgSAHelpers::readValue<uint_read_len_max>(*rlOffSrc, offset, plainTextReadMode);
                entry.advanceEntryByOffset(offset, idx, revComp == 1);
            } else {
                uint_pg_len_max pos;
                PgSAHelpers::readValue<uint_pg_len_max>(*rlPosSrc, pos, plainTextReadMode);
                entry.advanceEntryByPosition(pos, idx, revComp == 1);
            }
            if (rlMisCntSrc) {
                uint8_t mismatchesCount;
                PgSAHelpers::readValue<uint8_t>(*rlMisCntSrc, mismatchesCount, plainTextReadMode);
                for(uint8_t i = 0; i < mismatchesCount; i++) {
                    uint8_t mismatchCode;
                    uint_read_len_max mismatchOffset;
                    PgSAHelpers::readValue<uint8_t>(*rlMisSymSrc, mismatchCode, plainTextReadMode);
                    if (rlMisOffSrc)
                        PgSAHelpers::readValue<uint_read_len_max>(*rlMisOffSrc, mismatchOffset, plainTextReadMode);
                    else
                        PgSAHelpers::readValue<uint_read_len_max>(*rlMisRevOffSrc, mismatchOffset, plainTextReadMode);
                    entry.addMismatch(mismatchCode, mismatchOffset);
                }
                if (!rlMisOffSrc)
                    convertMisOffsets2RevOffsets(entry.mismatchOffset, entry.mismatchesCount, pgh->getMaxReadLength());
            }
            return true;
        }
        return false;
    }

    PgTools::ReadsListEntry<255, uint_read_len_max, uint_reads_cnt_max, uint_pg_len_max> &
    SeparatedExtendedReadsListIterator::peekReadEntry() {
        return entry;
    }

    bool SeparatedExtendedReadsListIterator::isRevCompEnabled() {
        return rlRevCompSrc;
    }

    bool SeparatedExtendedReadsListIterator::areMismatchesEnabled() {
        return rlMisCntSrc;
    }
}