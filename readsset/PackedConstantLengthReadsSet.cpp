#include "PackedConstantLengthReadsSet.h"

#include "tools/ReadsSetAnalizer.h"

namespace PgSAReadsSet {

    PackedConstantLengthReadsSet::PackedConstantLengthReadsSet(uint_read_len_max readLength, char *symbolsList,
                                                               uint8_t symbolsCount) {
        properties->maxReadLength = readLength;
        properties->minReadLength = readLength;
        properties->constantReadLength = true;
        properties->symbolsCount = symbolsCount;
        strcpy(properties->symbolsList, symbolsList);
        properties->generateSymbolOrder();
        properties->readsCount = 0;
        properties->allReadsLength = 0;

        uchar symbolsPerElement = SymbolsPackingFacility<uint_ps_element_min>::maxSymbolsPerElement(
                properties->symbolsCount);
        sPacker = new SymbolsPackingFacility<uint_ps_element_min>(properties, symbolsPerElement);

        packedLength = (properties->maxReadLength + symbolsPerElement - 1) / symbolsPerElement;
    }

    void PackedConstantLengthReadsSet::reserve(uint_reads_cnt_max readsCount) {
        packedReads.reserve((size_t) packedLength * readsCount);
    }

    void PackedConstantLengthReadsSet::addRead(const char* read, uint_read_len_max readLength) {
        if (readLength != properties->minReadLength) {
            fprintf(stderr, "Unsupported variable length reads.\n");
            exit(EXIT_FAILURE);
        }
        packedReads.resize((size_t) packedLength * ++properties->readsCount);
        properties->allReadsLength += properties->minReadLength;
        uint_ps_element_min *packedReadsPtr = packedReads.data() + packedLength * (properties->readsCount - 1);
        sPacker->packSequence(read, readLength, packedReadsPtr);
    }

    template<class ReadsSourceIterator>
    PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet(ReadsSourceIterator* readsIterator,
                                                                             ReadsSetProperties *properties) {
        bool ownProperties = properties == 0;
        if (ownProperties)
            properties = ReadsSetAnalizer::analizeReadsSet(readsIterator);

        if (!properties->constantReadLength) {
            cout << "Unsupported: variable length reads :(" << endl;
            cout << "Trimming reads to " << properties->minReadLength << " symbols." << endl;
            properties->constantReadLength = true;
            properties->maxReadLength = properties->minReadLength;
            properties->allReadsLength = properties->minReadLength * properties->readsCount;
        }

        PackedConstantLengthReadsSet* readsSet = new PackedConstantLengthReadsSet(properties->maxReadLength, properties->symbolsList, properties->symbolsCount);
        readsSet->reserve(properties->readsCount);

        readsIterator->rewind();
        while (readsIterator->moveNext())
            readsSet->addRead(readsIterator->getRead().data(), properties->minReadLength);

        if (ownProperties)
            delete(properties);

        return readsSet;
    }

    PackedConstantLengthReadsSet::~PackedConstantLengthReadsSet() {
        delete sPacker;
    }

    int PackedConstantLengthReadsSet::comparePackedReads(uint_reads_cnt_max lIdx, uint_reads_cnt_max rIdx){
        return sPacker->compareSequences(packedReads.data() + lIdx * (size_t) packedLength, packedReads.data() + rIdx * (size_t) packedLength, properties->maxReadLength);
    }

    int PackedConstantLengthReadsSet::comparePackedReads(uint_reads_cnt_max lIdx, uint_reads_cnt_max rIdx, uint_read_len_max offset) {
        return sPacker->compareSequences(packedReads.data() + lIdx * (size_t) packedLength, packedReads.data() + rIdx * (size_t) packedLength, offset, properties->maxReadLength - offset);
    }

    int PackedConstantLengthReadsSet::compareSuffixWithPrefix(uint_reads_cnt_max sufIdx, uint_reads_cnt_max preIdx, uint_read_len_max sufOffset) {
        return sPacker->compareSuffixWithPrefix(packedReads.data() + sufIdx * (size_t) packedLength, packedReads.data() + preIdx * (size_t) packedLength, sufOffset, properties->maxReadLength - sufOffset);
    }

    int PackedConstantLengthReadsSet::comparePackedReadWithPattern(const uint_reads_cnt_max i, const char *pattern) {
        return sPacker->compareSequenceWithUnpacked(packedReads.data() + i * (size_t) packedLength, pattern, properties->maxReadLength);
    }

    uint8_t PackedConstantLengthReadsSet::countMismatchesVsPattern(uint_reads_cnt_max i, const char *pattern, uint_read_len_max length,
                                                           uint8_t maxMismatches) {
        return sPacker->countSequenceMismatchesVsUnpacked(packedReads.data() + i * (size_t) packedLength, pattern, length, maxMismatches);
    }

    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<ReadsSourceIteratorTemplate<uint_read_len_min>>(ReadsSourceIteratorTemplate<uint_read_len_min>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<ReadsSourceIteratorTemplate<uint_read_len_std>>(ReadsSourceIteratorTemplate<uint_read_len_std>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<ConcatenatedReadsSourceIterator<uint_read_len_min>>(ConcatenatedReadsSourceIterator<uint_read_len_min>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<ConcatenatedReadsSourceIterator<uint_read_len_std>>(ConcatenatedReadsSourceIterator<uint_read_len_std>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<FASTAReadsSourceIterator<uint_read_len_min>>(FASTAReadsSourceIterator<uint_read_len_min>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<FASTAReadsSourceIterator<uint_read_len_std>>(FASTAReadsSourceIterator<uint_read_len_std>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<FASTQReadsSourceIterator<uint_read_len_min>>(FASTQReadsSourceIterator<uint_read_len_min>* readsIterator, ReadsSetProperties* properties);
    template PackedConstantLengthReadsSet* PackedConstantLengthReadsSet::loadReadsSet<FASTQReadsSourceIterator<uint_read_len_std>>(FASTQReadsSourceIterator<uint_read_len_std>* readsIterator, ReadsSetProperties* properties);

}