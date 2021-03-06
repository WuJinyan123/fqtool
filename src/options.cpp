#include "options.h"

Options::Options(){
    version = "0.0.0";
    compile = std::string(__TIME__) + " " + std::string(__DATE__);
    in1 = "";
    in2 = "";
    out1 = "";
    out2 = "";
    reportTitle = "Fastq Report";
    thread = 4;
    compression = 3;
    phred64 = false;
    inputFromSTDIN = false;
    outputToSTDOUT = false;
    interleavedInput = false;
    insertSizeMax = 512;
    overlapDiffLimit = 5;
    overlapRequire = 30;
    jsonFile = "report.json";
    htmlFile = "report.html";
}

void Options::update(int argc, char** argv){
    // convert to internal Phred33 based quality score for performance
    qualFilter.lowQualityLimit = qualFilter.lowQualityLimit + 33;
    // update adapter cutting options
    adapter.adapterSeqR1Provided = adapter.inputAdapterSeqR1.empty() ? false : true;
    adapter.adapterSeqR2Provided = adapter.inputAdapterSeqR2.empty() ? false : true;
    adapter.cutable = (adapter.enableTriming && (isPaired() || adapter.inputAdapterSeqR1.length() > 0));
    if(adapter.enableTriming && (!adapter.adapterSeqR1Provided && !adapter.adapterSeqR2Provided) && isPaired()){
        adapter.enableDetectForPE = true;
    }
    // update index filtering options
    if(indexFilter.enabled){
        initIndexFilter(indexFilter.index1File, indexFilter.index2File, indexFilter.threshold);
    }
    if(indexFilter.enabled){
        initIndexFilter(indexFilter.index1File, indexFilter.index2File, indexFilter.threshold);
    }
    // update split potions
    split.enabled = split.byFileLines || split.byFileNumber;
    // update quality filter options
    qualFilter.lowQualityBaseLimit = qualFilter.lowQualityRatio * est.seqLen1;
    // validate umi options
    if(umi.enabled && (umi.location == 3 || umi.location == 4 || umi.location == 6) && umi.length == 0){
        util::errorExit("umi length can not be zero if it's in read1/2");
    }
    // update polyx
    util::str2upper(polyXTrim.trimChr);
    // update command
    for(int i = 0; i < argc; ++i){
        command.append(argv[i]);
        command.append(" ");
    }
    // update cwd
    cwd = util::cwd();
}

void Options::validate(){
    // validate merged file
    if(mergePE.enabled){
        if(mergePE.out.empty()){
            util::errorExit("merged file output must be provided!");
        }
    }
    // validate polyX
    if(polyXTrim.trimChr.find_first_not_of("ATCGN") != std::string::npos){
        util::errorExit("Can only trim nucleotides ATCGN");
    }
}

bool Options::isPaired(){
    return in2.length() > 0 || interleavedInput;
}

void Options::initIndexFilter(const std::string& blacklistFile1, const std::string& blacklistFile2, int threshold){
    if(blacklistFile1.empty() && blacklistFile2.empty()){
        return;
    }
    if(!blacklistFile1.empty()){
        util::validFile(blacklistFile1);
        indexFilter.blacklist1 = makeListFromFileByLine(blacklistFile1);
    }
    if(!blacklistFile2.empty()){
        util::validFile(blacklistFile2);
        indexFilter.blacklist2 = makeListFromFileByLine(blacklistFile2);
    }
    if(indexFilter.blacklist1.empty() && indexFilter.blacklist2.empty()){
        return;
    }
    indexFilter.enabled = true;
    indexFilter.threshold = threshold;
}

std::vector<std::string> Options::makeListFromFileByLine(const std::string& filename){
    std::vector<std::string> ret;
    std::ifstream fr(filename);
    std::string line;
    while(std::getline(fr, line)){
        util::strip(line);
        if(line.find_first_not_of("ATCG") != std::string::npos){
            util::errorExit("processing " + filename + ", each line should be one index, which can only contain A/T/C/G");
        }
        ret.push_back(line);
    }
    return ret;
}
