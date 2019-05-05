#include "peprocessor.h"

PairEndProcessor::PairEndProcessor(Options* opt){
    mOptions = opt;
    mProduceFinished = false;
    mFinishedThreads = 0;
    mFilter = new Filter(opt);
    mOutStream1 = NULL;
    mOutStream2 = NULL;
    mZipFile1 = NULL;
    mZipFile2 = NULL;
    mUmiProcessor = new UmiProcessor(mOptions);

    int insertSizeBufLen = mOptions->insertSizeMax + 1;
    mInsertSizeHist = new long[insertSizeBufLen];
    std::memset(mInsertSizeHist, 0, sizeof(long) * insertSizeBufLen);
    mLeftWriter = NULL;
    mRightWriter = NULL;
    mUnPairedLeftWriter = NULL;
    mUnPairedRightWriter = NULL;
    mMergedWriter = NULL;
    mFailedWriter = NULL;

    mDuplicate = NULL;
    if(mOptions->duplicate.enabled){
        mDuplicate = new Duplicate(mOptions);
    }
}

PairEndProcessor::~PairEndProcessor(){
    delete mInsertSizeHist;
    delete mUmiProcessor;
    if(mDuplicate){
        delete mDuplicate;
        mDuplicate = NULL;
    }
}

void PairEndProcessor::initOutput(){
    if(!mOptions->unpaired1.empty()){
        mUnPairedLeftWriter = new WriterThread(mOptions, mOptions->unpaired1);
    }
    if(!mOptions->unpaired2.empty() && mOptions->unpaired2 != mOptions->unpaired1){
        mUnPairedRightWriter = new WriterThread(mOptions, mOptions->unpaired2);
    }
    if(mOptions->mergePE.enabled){
        if(!mOptions->mergePE.out.empty()){
            mMergedWriter = new WriterThread(mOptions, mOptions->mergePE.out);
        }
    }
    if(!mOptions->failedOut.empty()){
        mFailedWriter = new WriterThread(mOptions, mOptions->failedOut);
    }
    if(mOptions->out1.empty()){
        return;
    }
    mLeftWriter = new WriterThread(mOptions, mOptions->out1);
    if(mOptions->out2.empty()){
        mRightWriter = new WriterThread(mOptions, mOptions->out2);
    }
}

void PairEndProcessor::closeOutput(){
    if(mLeftWriter){
        delete mLeftWriter;
        mLeftWriter = NULL;
    }
    if(mRightWriter){
        delete mRightWriter;
        mRightWriter = NULL;
    }
    if(mMergedWriter){
        delete mMergedWriter;
        mMergedWriter = NULL;
    }
    if(mFailedWriter){
        delete mFailedWriter;
        mFailedWriter = NULL;
    }
    if(mUnPairedLeftWriter){
        delete mUnPairedLeftWriter;
        mUnPairedLeftWriter = NULL;
    }
    if(mUnPairedRightWriter){
        delete mUnPairedRightWriter;
        mUnPairedRightWriter = NULL;
    }
}

void PairEndProcessor::initConfig(ThreadConfig* config){
    if(mOptions->out1.empty() || mOptions->out2.empty()){
        return;
    }
    if(mOptions->split.enabled){
        config->initWriterForSplit();
    }
}

bool PairEndProcessor::process(){
    // construct two Writer for read1/2 if split disabled
    if(!mOptions->split.enabled){
        initOutput();
    }
    // initialization initReadPairPackRepository
    initReadPairPackRepository();
    // start single producerTask produce pack in background
    std::thread producer(&PairEndProcessor::producerTask, this);
    // start multiple thread consumerTask in background
    ThreadConfig** configs = new ThreadConfig*[mOptions->thread];
    for(int t = 0; t < mOptions->thread; ++t){
        configs[t] = new ThreadConfig(mOptions, t, true);
        initConfig(configs[t]);
    }
    std::thread** threads = new std::thread*[mOptions->thread];
    for(int t = 0; t < mOptions->thread; ++t){
        threads[t] = new std::thread(&PairEndProcessor::consumerTask, this, configs[t]);
    }
    // start read1/2 writer thread in background if split disabled
    std::thread* leftWriterThread = NULL;
    std::thread* rightWriterThread = NULL;
    std::thread* unpairedLeftWriterThread = NULL;
    std::thread* unpairedRightWriterThread = NULL;
    std::thread* mergedWriterThread = NULL;
    std::thread* failedWriterThread = NULL;
    if(mLeftWriter){
        leftWriterThread = new std::thread(&PairEndProcessor::writeTask, this, mLeftWriter);
    }
    if(mRightWriter){
        rightWriterThread = new std::thread(&PairEndProcessor::writeTask, this, mRightWriter);
    }
    if(mUnPairedLeftWriter){
        unpairedLeftWriterThread = new std::thread(&PairEndProcessor::writeTask, this, mUnPairedLeftWriter);
    }
    if(mUnPairedRightWriter){
        unpairedRightWriterThread = new std::thread(&PairEndProcessor::writeTask, this, mUnPairedRightWriter);
    }
    if(mFailedWriter){
       failedWriterThread  = new std::thread(&PairEndProcessor::writeTask, this, mFailedWriter);
    }
    if(mMergedWriter){
        mergedWriterThread = new std::thread(&PairEndProcessor::writeTask, this, mMergedWriter);
    }
    // join producerTask thread
    producer.join();
    // join consumerTask threads
    for(int t = 0; t < mOptions->thread; ++t){
        threads[t]->join();
    }
    // joint leftWriterThread and rightWriterThread etc
    if(!mOptions->split.enabled){
        if(leftWriterThread){
            leftWriterThread->join();
        }
        if(rightWriterThread){
            rightWriterThread->join();
        }
        if(unpairedLeftWriterThread){
            unpairedLeftWriterThread->join();
        }
        if(unpairedRightWriterThread){
            unpairedRightWriterThread->join();
        }
        if(mergedWriterThread){
            mergedWriterThread->join();
        }
        if(failedWriterThread){
            failedWriterThread->join();
        }
    }
    // summarize statistic informations
    std::vector<Stats*> preStats1;
    std::vector<Stats*> preStats2;
    std::vector<Stats*> postStats1;
    std::vector<Stats*> postStats2;
    std::vector<FilterResult*> filterResults;
    for(int t = 0; t < mOptions->thread; ++t){
        preStats1.push_back(configs[t]->getPreStats1());
        preStats2.push_back(configs[t]->getPreStats2());
        postStats1.push_back(configs[t]->getPostStats1());
        postStats2.push_back(configs[t]->getPostStats2());
        filterResults.push_back(configs[t]->getFilterResult());
    }
    Stats* finalPreStats1 = Stats::merge(preStats1);
    Stats* finalPreStats2 = Stats::merge(preStats2);
    Stats* finalPostStats1 = Stats::merge(postStats1);
    Stats* finalPostStats2 = Stats::merge(postStats2);
    FilterResult* finalFilterResult = FilterResult::merge(filterResults);
    std::cerr << "Read1 before filtering: \n";
    std::cerr << finalPreStats1 << "\n";
    std::cerr << "Read2 before filtering: \n";
    std::cerr << finalPreStats2 << "\n";
    if(!mOptions->mergePE.enabled){
        std::cerr << "Read1 after filtering: \n";
        std::cerr << finalPostStats1 << "\n";
        std::cerr << "Read2 after filtering: \n";
        std::cerr << finalPostStats2 << "\n";
    }else{
        std::cerr << "Filtering results: \n";
        std::cerr << finalFilterResult << "\n";
    }
    // duplication analysis
    size_t* dupHist = NULL;
    double* dupMeanGC = NULL;
    double dupRate = 0.0;
    if(mOptions->duplicate.enabled){
        dupHist = new size_t[mOptions->duplicate.histSize];
        std::memset(dupHist, 0, sizeof(int) * mOptions->duplicate.histSize);
        dupMeanGC = new double[mOptions->duplicate.histSize];
        std::memset(dupMeanGC, 0, sizeof(double) * mOptions->duplicate.histSize);
        dupRate = mDuplicate->statAll(dupHist, dupMeanGC, mOptions->duplicate.histSize);
        std::cerr << "\nDuplication rate: " << dupRate * 100.0 << "%\n";
    }

    int peakInsertSize = getPeakInsertSize();
    std::cerr << "Insert size peak (evaluated by pair-end reads: " << peakInsertSize << ")\n";

    if(mOptions->mergePE.enabled){
        std::cerr << "Read pairs merged: " << finalFilterResult->mMergedPairs << "\n";
        if(finalPostStats1->getReads() > 0){
            double postMergedPercent = 100.0 * finalFilterResult->mMergedPairs / finalPostStats1->getReads();
            double preMergedPercent = 100.0 * finalFilterResult->mMergedPairs / finalPreStats1->getReads();
            std::cerr << "% of original read pairs: " << preMergedPercent << "%\n";
            std::cerr << "% in reads after filtering: " << postMergedPercent << "%\n";
        }
    }

    JsonReporter jr(mOptions);
    jr.setDupHist(dupHist, dupMeanGC, dupRate);
    jr.setInsertHist(mInsertSizeHist, peakInsertSize);
    jr.report(finalFilterResult,finalPreStats1, finalPostStats1, finalPreStats2, finalPostStats2);
    HtmlReporter hr(mOptions);
    hr.setDupHist(dupHist, dupMeanGC, dupRate);
    hr.report(finalFilterResult,finalPreStats1, finalPostStats1, finalPreStats2, finalPostStats2);

    // clean up
    for(int t = 0; t < mOptions->thread; ++t){
        delete threads[t];
        threads[t] = NULL;
        delete configs[t];
        configs[t] = NULL;
    }
    delete finalPreStats1;
    delete finalPreStats2;
    delete finalPostStats1;
    delete finalPostStats2;
    delete finalFilterResult;
    if(mOptions->duplicate.enabled){
        delete[] dupHist;
        delete[] dupMeanGC;
    }
    delete[] threads;
    delete[] configs;
    if(leftWriterThread){
        delete leftWriterThread;
    }
    if(rightWriterThread){
        delete rightWriterThread;
    }
    if(!mOptions->split.enabled){
        closeOutput();
    }
    return true;
}

int PairEndProcessor::getPeakInsertSize(){
    int peak = 0;
    long maxCount = -1;
    for(int i = 0; i < mOptions->insertSizeMax; ++i){
        if(mInsertSizeHist[i] > maxCount){
            peak = i;
            maxCount = mInsertSizeHist[i];
        }
    }
    return peak;
}

bool PairEndProcessor::processPairEnd(ReadPairPack* pack, ThreadConfig* config){
    std::string outstr1;
    std::string outstr2;
    std::string failedOut;
    std::string unpairedOut1;
    std::string unpairedOut2;
    std::string mergedOutput; 
    std::string singleOutput;
    int readPassed = 0;
    int mergedCount = 0;
    for(int p = 0; p < pack->count; ++p){
        ReadPair* pair = pack->data[p];
        Read* or1 = pair->left;
        Read* or2 = pair->right;
        
        config->getPreStats1()->statRead(or1);
        config->getPreStats2()->statRead(or2);

        if(mOptions->duplicate.enabled){
            mDuplicate->statPair(or1, or2);
        }

        if(mOptions->indexFilter.enabled && mFilter->filterByIndex(or1, or2)){
            delete pair;
            continue;
        }

        if(mOptions->umi.enabled){
            mUmiProcessor->process(or1, or2);
        }

        Read* r1 = mFilter->trimAndCut(or1, mOptions->trim.front1, mOptions->trim.tail1);
        Read* r2 = mFilter->trimAndCut(or2, mOptions->trim.front2, mOptions->trim.tail2);

        if(r1 && r2){
            if(mOptions->polyGTrim.enabled){
                PolyX::trimPolyG(r1, r2, mOptions->polyGTrim.minLen);
            }
        }

        bool insertSizeEvaluated = false;
        if(r1 && r2 && (mOptions->adapter.enableTriming || mOptions->correction.enabled)){
            OverlapResult ov = OverlapAnalysis::analyze(r1, r2, mOptions->overlapDiffLimit, mOptions->overlapRequire);
            if(config->getThreadId() == 0){
                statInsertSize(r1, r2, ov);
                insertSizeEvaluated = true;
            }
            if(mOptions->correction.enabled){
                BaseCorrector::correctByOverlapAnalysis(r1, r2, config->getFilterResult(), ov);
            }
            if(mOptions->adapter.enableTriming){
                bool trimmed = AdapterTrimmer::trimByOverlapAnalysis(r1, r2, config->getFilterResult(), ov);
                if(!trimmed){
                    if(mOptions->adapter.adapterSeqR1Provided){
                        AdapterTrimmer::trimBySequence(r1, config->getFilterResult(), mOptions->adapter.inputAdapterSeqR1, false);
                    }
                    if(mOptions->adapter.adapterSeqR2Provided){
                        AdapterTrimmer::trimBySequence(r2, config->getFilterResult(), mOptions->adapter.inputAdapterSeqR2, true);
                    }
                }
            }
        }

        if(config->getThreadId() == 0 && !insertSizeEvaluated && r1 != NULL && r2!=NULL){
              OverlapResult ov = OverlapAnalysis::analyze(r1, r2, mOptions->overlapDiffLimit, mOptions->overlapRequire);
              statInsertSize(r1, r2, ov);
              insertSizeEvaluated = true;
        }

        if(r1 && r2){
            if(mOptions->polyXTrim.enabled){
                PolyX::trimPolyX(r1, r2, mOptions->polyXTrim.minLen);
            }
        }

        if(r1 && r2){
            if(mOptions->trim.maxLen1 > 0 && mOptions->trim.maxLen1 < r1->length()){
                r1->resize(mOptions->trim.maxLen1);
            }
            if(mOptions->trim.maxLen2 > 0 && mOptions->trim.maxLen2 < r2->length()){
                r2->resize(mOptions->trim.maxLen2);
            }
        }

        Read* merged = NULL;
        bool mergeProcessed = false;
        if(mOptions->mergePE.enabled && r1 && r2){
            OverlapResult ov = OverlapAnalysis::analyze(r1, r2, mOptions->overlapDiffLimit, mOptions->overlapRequire);
            if(ov.overlapped){
                merged = OverlapAnalysis::merge(r1, r2, ov);
                int result = mFilter->passFilter(merged);
                config->addFilterResult(result, 2);
                if(result == COMMONCONST::PASS_FILTER){
                    mergedOutput += merged->toString();
                    config->getPostStats1()->statRead(merged);
                    ++readPassed;
                    ++mergedCount;
                }
                delete merged;
                mergeProcessed = true;
            }else if(!mOptions->mergePE.discardUnmerged){
                int result1 = mFilter->passFilter(r1);
                config->addFilterResult(result1, 1);
                if(result1 == COMMONCONST::PASS_FILTER){
                    mergedOutput += r1->toString();
                    config->getPostStats1()->statRead(r1);
                }
                int result2 = mFilter->passFilter(r2);
                config->addFilterResult(result2, 1);
                if(result2 == COMMONCONST::PASS_FILTER){
                    mergedOutput += r2->toString();
                    config->getPostStats2()->statRead(r2);
                }
                if(result1 == COMMONCONST::PASS_FILTER && result2 == COMMONCONST::PASS_FILTER){
                    ++readPassed;
                }
                mergeProcessed = true;
            }
        }

        if(!mergeProcessed){
            int result1 = mFilter->passFilter(r1);
            int result2 = mFilter->passFilter(r2);
            config->addFilterResult(std::max(result1, result2));

            if(r1 && result1 == COMMONCONST::PASS_FILTER && r2 && result2 == COMMONCONST::PASS_FILTER){
                if(mOptions->outputToSTDOUT){
                    singleOutput += r1->toString() + r2->toString();
                }else{
                    outstr1 += r1->toString();
                    outstr2 += r2->toString();
                }
                if(!mOptions->mergePE.enabled){
                    config->getPostStats1()->statRead(r1);
                    config->getPostStats2()->statRead(r2);
                }
                ++readPassed;
            }else if(r1 && result1 == COMMONCONST::PASS_FILTER){
                if(mUnPairedLeftWriter){
                    unpairedOut1 += r1->toString();
                    if(mFailedWriter){
                        failedOut += or2->toStringWithTag(COMMONCONST::FAILED_TYPES[result2]);
                    }
                }else{
                    if(mFailedWriter){
                        failedOut += or1->toStringWithTag("paired_read_is_failing");
                        failedOut += or2->toStringWithTag(COMMONCONST::FAILED_TYPES[result2]);
                    }
                }
            }else if(r2 && result2 == COMMONCONST::PASS_FILTER){
                if(mUnPairedLeftWriter){
                    unpairedOut2 += r2->toString();
                    if(mFailedWriter){
                        failedOut += or1->toStringWithTag(COMMONCONST::FAILED_TYPES[result2]);
                    }
                }else{
                    if(mFailedWriter){
                        failedOut += or1->toStringWithTag(COMMONCONST::FAILED_TYPES[result1]);
                        failedOut += or2->toStringWithTag("paired_read_is_failing");
                    }
                }
            }
        }

        delete pair;
        if(r1 && r1 != or1){
            delete r1;
        }
        if(r2 && r2 != or2){
            delete r2;
        }
    }

    if(!mOptions->split.enabled){
        mOutputMtx.lock();
    }
    if(mOptions->outputToSTDOUT){
        if(mOptions->mergePE.enabled){
            std::fwrite(mergedOutput.c_str(), 1, mergedOutput.length(), stdout);
        }else{
            std::fwrite(singleOutput.c_str(), 1, singleOutput.size(), stdout);
        }
    }else if(mOptions->split.enabled){
        if(!mOptions->out1.empty()){
            config->getWriter1()->writeString(outstr1);
        }
        if(!mOptions->out2.empty()){
            config->getWriter2()->writeString(outstr2);
        }
    }
    
    if(mMergedWriter && !mergedOutput.empty()){
        char* mdata = new char[mergedOutput.size()];
        std::memcpy(mdata, mergedOutput.c_str(), mergedOutput.size());
        mMergedWriter->input(mdata, mergedOutput.size());
    }

    if(mFailedWriter && !failedOut.empty()){
        char* fdata = new char[failedOut.size()];
        std::memcpy(fdata, failedOut.c_str(), failedOut.size());
        mFailedWriter->input(fdata, failedOut.size());
    }

    if(mRightWriter && mLeftWriter && (!outstr1.empty() || !outstr2.empty())){
        char* ldata = new char[outstr1.size()];
        std::memcpy(ldata, outstr1.c_str(), outstr1.size());
        mLeftWriter->input(ldata, outstr1.size());
        char* rdata = new char[outstr2.size()];
        std::memcpy(rdata, outstr2.c_str(), outstr2.size());
        mRightWriter->input(rdata, outstr2.size());
    }else if(mLeftWriter && !singleOutput.empty()){
        char* ldata = new char[singleOutput.size()];
        std::memcpy(ldata, singleOutput.c_str(), singleOutput.size());
        mLeftWriter->input(ldata, singleOutput.size());
    }

    if(!unpairedOut1.empty() && mUnPairedLeftWriter){
        char* unpairedData1 = new char[unpairedOut1.size()];
        std::memcpy(unpairedData1, unpairedOut1.c_str(), unpairedOut1.size());
        mUnPairedLeftWriter->input(unpairedData1, unpairedOut1.size());
    }
    if(!unpairedOut2.empty() && mUnPairedRightWriter){
        char* unpairedData2 = new char[unpairedOut2.size()];
        std::memcpy(unpairedData2, unpairedOut2.c_str(), unpairedOut2.size());
        mUnPairedRightWriter->input(unpairedData2, unpairedOut2.size());
    }

    if(!mOptions->split.enabled){
        mOutputMtx.unlock();
    }
    if(mOptions->split.byFileLines){
        config->markProcessed(readPassed);
    }else{
        config->markProcessed(pack->count);
    }
    if(mOptions->mergePE.enabled){
        config->addMergedPairs(mergedCount);
    }
    delete[] pack->data;
    delete pack;
    
    return true;
}

void PairEndProcessor::statInsertSize(Read* r1, Read* r2, OverlapResult& ov){
    int isize = mOptions->insertSizeMax;
    if(ov.overlapped){
        if(ov.offset > 0){
            isize = r1->length() + r2->length() - ov.overlapLen;
        }else{
            isize = ov.overlapLen;
        }
    }
    if(isize > mOptions->insertSizeMax){
        isize = mOptions->insertSizeMax;
    }
    ++mInsertSizeHist[isize];
}

void PairEndProcessor::initReadPairPackRepository(){
    mRepo.packBuffer = new ReadPairPack*[mOptions->bufSize.maxPacksInReadPackRepo];
    std::memset(mRepo.packBuffer, 0, sizeof(ReadPairPack*) * mOptions->bufSize.maxPacksInReadPackRepo);
    mRepo.writePos = 0;
    mRepo.readPos = 0;
}

void PairEndProcessor::destroyReadPairPackRepository(){
    delete[] mRepo.packBuffer;
    mRepo.packBuffer = NULL;
}

void PairEndProcessor::producePack(ReadPairPack* pack){
    if(mRepo.writePos >= mOptions->bufSize.maxPacksInReadPackRepo){
    }
    while(mRepo.writePos >= mOptions->bufSize.maxPacksInReadPackRepo){
        usleep(1000);
    }
    mRepo.packBuffer[mRepo.writePos] = pack;
    ++mRepo.writePos;
}

void PairEndProcessor::consumePack(ThreadConfig* config){
    mInputMtx.lock();
    while(mRepo.writePos <= mRepo.readPos){
        usleep(1000);
        if(mProduceFinished){
            mInputMtx.unlock();
            return;
        }
    }
    ReadPairPack* data = mRepo.packBuffer[mRepo.readPos];
    ++mRepo.readPos;
    if(mRepo.readPos >= mOptions->bufSize.maxPacksInReadPackRepo){
        mRepo.readPos = mRepo.readPos %  mOptions->bufSize.maxPacksInReadPackRepo;
        mRepo.writePos = mRepo.writePos % mOptions->bufSize.maxPacksInReadPackRepo;
        processPairEnd(data, config);
        mInputMtx.unlock();
    }else{
        mInputMtx.unlock();
        processPairEnd(data, config);
    }
}

void PairEndProcessor::producerTask(){
    util::loginfo("loading  data started", mOptions->logmtx);
    size_t lastReported = 0;
    size_t slept = 0;
    size_t readNum = 0;
    ReadPair** data = new ReadPair*[mOptions->bufSize.maxReadsInPack];
    std::memset(data, 0, sizeof(ReadPair*) * mOptions->bufSize.maxReadsInPack);
    FqReaderPair reader(mOptions->in1, mOptions->in2, true, mOptions->phred64, mOptions->interleavedInput);
    size_t count = 0;
    bool needToBreak = false;
    while(true){
        ReadPair* readPair = reader.read();
        if(!readPair || needToBreak){
            ReadPairPack* pack = new ReadPairPack;
            pack->data = data;
            pack->count = count;
            producePack(pack);
            data = NULL;
            if(readPair){
                delete readPair;
                readPair = NULL;
            }
            break;
        }
        data[count] = readPair;
        ++count;
        if(mOptions->readsToProcess > 0 && count + readNum >= mOptions->readsToProcess){
            needToBreak = true;
        }
        if(count + readNum >= lastReported + 1000000){
            lastReported = count + readNum;
            std::string msg = "loaded " + std::to_string(lastReported/1000000) + "M";
            util::loginfo(msg, mOptions->logmtx);
        }
        if(count == mOptions->bufSize.maxReadsInPack || needToBreak){
            ReadPairPack* pack = new ReadPairPack;
            pack->data = data;
            pack->count = count;
            producePack(pack);
            data = new ReadPair*[mOptions->bufSize.maxReadsInPack];
            std::memset(data, 0, sizeof(ReadPair*) * mOptions->bufSize.maxReadsInPack);
            while(mRepo.writePos - mRepo.readPos > mOptions->bufSize.maxPacksInMemory){
                ++slept;
                usleep(1000);
            }
            readNum += count;
            if(readNum % (mOptions->bufSize.maxReadsInPack * mOptions->bufSize.maxPacksInMemory) == 0 && mLeftWriter){
                while( (mLeftWriter && mLeftWriter->bufferLength() > mOptions->bufSize.maxPacksInMemory) ||
                       (mRightWriter && mRightWriter->bufferLength() > mOptions->bufSize.maxPacksInMemory)){
                    ++slept;
                    usleep(1000);
                }
            }
            count = 0;
        }
    }
    mProduceFinished = true;
    util::loginfo("all data loaded, start to monitor thread status", mOptions->logmtx);
}

void PairEndProcessor::consumerTask(ThreadConfig* config){
    while(true){
        if(config->canBeStopped()){
            ++mFinishedThreads;
            break;
        }
        while(mRepo.writePos <= mRepo.readPos){
            if(mProduceFinished){
                break;
            }
            usleep(1000);
        }
        if(mProduceFinished && mRepo.writePos == mRepo.readPos){
            ++mFinishedThreads;
            std::string msg = "thread " + std::to_string(config->getThreadId() + 1) + " data processing completed";
            util::loginfo(msg, mOptions->logmtx);
            break;
        }
        if(mProduceFinished){
            std::string msg = "thread " + std::to_string(config->getThreadId() + 1) + " is processing the " +
                              std::to_string(mRepo.readPos) + " / " + std::to_string(mRepo.writePos) + " pack";
            util::loginfo(msg, mOptions->logmtx);
        }
        consumePack(config);
    }
    if(mFinishedThreads == mOptions->thread){
        if(mLeftWriter){
            mLeftWriter->setInputCompleted();
        }
        if(mRightWriter){
            mRightWriter->setInputCompleted();
        }
        if(mUnPairedLeftWriter){
            mUnPairedLeftWriter->setInputCompleted();
        }
        if(mUnPairedRightWriter){
            mUnPairedRightWriter->setInputCompleted();
        }
        if(mMergedWriter){
            mMergedWriter->setInputCompleted();
        }
        if(mFailedWriter){
            mFailedWriter->setInputCompleted();
        }
    }
    std::string msg = "thread " + std::to_string(config->getThreadId() + 1) + " finished";
    util::loginfo(msg, mOptions->logmtx);
}

void PairEndProcessor::writeTask(WriterThread* config){
    while(true){
        if(config->isCompleted()){
            config->output();
            break;
        }
        config->output();
    }
    std::string msg = config->getFilename() + " writer finished";
    util::loginfo(msg, mOptions->logmtx);
}
