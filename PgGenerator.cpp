#include "pseudogenome/generator/GreedySwipingPackedOverlapPseudoGenomeGenerator.h"
#include "pseudogenome/persistence/PseudoGenomePersistence.h"
#include "pseudogenome/persistence/SeparatedPseudoGenomePersistence.h"
#include "readsset/persistance/ReadsSetPersistence.h"
#include "readsset/iterator/DivisionReadsSetDecorators.h"

#include "helper.h"
#include <stdlib.h>    /* for exit */
#include <unistd.h>

using namespace PgTools;

PseudoGenomeBase *preparePg(string srcFile, string pairFile, string divisionFile) {
    PseudoGenomeBase* pgb = 0;

    if (PseudoGenomePersistence::isValidPseudoGenome(srcFile)) {
        pgb = PseudoGenomePersistence::readPseudoGenome(srcFile);
        cout << "Pseudogenome length: " << pgb->getPseudoGenomeLength() << endl;
        pgb->getReadsSetProperties()->printout();
        return pgb;
    }

    PseudoGenomeGeneratorFactory* pggf = new GreedySwipingPackedOverlapPseudoGenomeGeneratorFactory();
    ReadsSourceIteratorTemplate<uint_read_len_max> *readsIterator = ReadsSetPersistence::createManagedReadsIterator(
            srcFile, pairFile, divisionFile, true);

    PseudoGenomeGeneratorBase* pggb = pggf->getGenerator(readsIterator);
    pgb = pggb->generatePseudoGenomeBase();
    delete(pggb);
    delete(pggf);
    
    if (pgb == 0) {
        fprintf(stderr, "Failed generating Pg\n");
        exit(EXIT_FAILURE);    
    }
    
    return pgb;
}

bool verifyPg(string srcFile, string pairFile, string pgFile) {
    PseudoGenomeBase* pgb = 0;
    
    if (access( pgFile.c_str(), F_OK ) != -1 && PseudoGenomePersistence::isValidPseudoGenome(pgFile))
        pgb = PseudoGenomePersistence::readPseudoGenome(pgFile);
    else {
        fprintf(stderr, (pgFile + " is not a valid pseudogenome file.\n").c_str());
        return false;
    }

    ReadsSourceIteratorTemplate<uint_read_len_max> *readsIterator = ReadsSetPersistence::createManagedReadsIterator(
            srcFile, pairFile);
    DefaultReadsSet* readsSet = new DefaultReadsSet(readsIterator);
    delete(readsIterator);
    bool isValid = pgb->validateUsing(readsSet);
    delete readsSet;
    
    return isValid;
}

int main(int argc, char *argv[]) {

    int opt; // current option
    bool sFlag = false;
    bool vFlag = false;
    bool tFlag = false;
    string divisionFile = "";

    while ((opt = getopt(argc, argv, "d:svt?")) != -1) {
        switch (opt) {
            case 'd':
                divisionFile = optarg;
                break;
            case 's':
                sFlag = true;
                break;
            case 'v':
                vFlag = true;
                break;
            case 't':
                plainTextWriteMode = true;
                plainTextReadMode = true;
                break;
            case '?':
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-s] [-v] [-t] [-d divisionfile] readssrcfile [pairsrcfile] pgprefix\n\n",
                        argv[0]);
                fprintf(stderr, "-s separate Pg and reads list, no SA\n-v validate (use after generation)\n-t write numbers in text mode\n\n");
                exit(EXIT_FAILURE);
        }
    }

    if (optind > (argc - 2) || optind < (argc - 3)) {
        fprintf(stderr, "%s: Expected 2 or 3 arguments after options (found %d)\n", argv[0], argc - optind);
        fprintf(stderr, "try '%s -?' for more information\n", argv[0]);

        exit(EXIT_FAILURE);
    }

    string srcFile(argv[optind++]);
    string pairFile = "";
    if (optind == argc - 2)
        pairFile = argv[optind++];
    string idxPrefix(argv[optind++]);

    if (vFlag) {
        fprintf(stderr, "Verification started...\n");
        if (sFlag) {
            if (verifyPg(srcFile, pairFile, idxPrefix + PseudoGenomeBase::PSEUDOGENOME_FILE_SUFFIX)) {
                fprintf(stderr, "Pseudogenome is correct.\n");
                exit(EXIT_SUCCESS);
            }
            fprintf(stderr, "Pseudogenome validation failed.\n");
            exit(EXIT_FAILURE);
        }

        fprintf(stderr, "Error: Option not implemented.\n");
        exit(EXIT_FAILURE);
    }

    PseudoGenomeBase *pgb = preparePg(srcFile, pairFile, divisionFile);

    if (sFlag) {
        PgTools::SeparatedPseudoGenomePersistence::writePseudoGenome(pgb, idxPrefix);
    }else {
        PseudoGenomePersistence::writePseudoGenome(pgb, idxPrefix);
    }
    delete(pgb);
    
    exit(EXIT_SUCCESS);
}