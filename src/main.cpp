#include <string>
#include "CLI.hpp"
#include "options.h"
#include "evaluator.h"
#include "processor.h"

int main(int argc, char** argv){
    std::string sysCMD = std::string(argv[0]) + " -h";
    if(argc == 1){
        std::system(sysCMD.c_str());
        return 0;
    }
    
    Options* opt = new Options();
    // I/O
    CLI::App app("program: " + std::string(argv[0]) + "\nversion: " + opt->version + "\nupdated: " + opt->compile);
    app.get_formatter()->column_width(80);
    CLI::Option* pin1 = app.add_option("-i,--in1", opt->in1, "read1 input file name")->required(true)->check(CLI::ExistingFile);
    app.add_option("-o,--out1", opt->out1, "read1 output file name")->required(true);
    CLI::Option* pin2 = app.add_option("-I,--in2", opt->in2, "read2 input file name")->needs(pin1)->check(CLI::ExistingFile);
    app.add_option("-O,--out2", opt->out2, "read2 output file name")->needs(pin2);
    CLI::Option* pmerge = app.add_flag("-m,--merge", opt->mergePE.enabled, "merge overlapped pair of read into one");
    app.add_flag("-d,--discard_unmerged", opt->mergePE.discardUnmerged, "discard unmerged reads")->needs(pmerge);
    app.add_flag("--phred64", opt->phred64, "input fastq quality is in phred64 mode");
    app.add_option("-z,--compress_level", opt->compression, "compression level for gzip output")->check(CLI::Range(1, 9));
    app.add_flag("--inverleaved_in", opt->interleavedInput, "input is an interleaved FASTQ")->excludes(pin2);
    app.add_flag("--notoverwrite", opt->donotOverwrite, "do not overwriting existing output");
    app.add_flag("-v,--verbose", opt->verbose, "output verbose log information");
    // adapter
    CLI::Option* pcutadapter = app.add_flag("-A,--enable_adapter_trimming", opt->adapter.enableTriming, "enable adapter trimming");
    app.add_option("--adapter_seqr1", opt->adapter.inputAdapterSeqR1, "adapter for read1")->needs(pcutadapter);
    app.add_option("--adapter_seqr2", opt->adapter.inputAdapterSeqR2, "adapter for read2")->needs(pcutadapter);
    app.add_flag("--detect_pe_adapter", opt->adapter.enableDetectForPE, "enable detect adapter for PE reads")->needs(pin2);
    // trimming
    app.add_option("-f,--trim_front1", opt->trim.front1, "#bases trimmed in front for read1", true)->check(CLI::Range(0, 1000));
    app.add_option("-t,--trim_tail1", opt->trim.tail1, "#bases trimmed in tail for read1", true)->check(CLI::Range(0, 1000));
    app.add_option("-b,--max_len1", opt->trim.maxLen1, "maximum length keep for read1 after trim tail", true)->check(CLI::Range(0, 1000));
    app.add_option("-F,--trim_front2", opt->trim.front2, "#bases trimmed in front for read2", true)->check(CLI::Range(0, 1000));
    app.add_option("-T,--trim_tail2", opt->trim.tail2, "#bases trimmed in tail for read2", true)->check(CLI::Range(0, 1000));
    app.add_option("-B,--max_len2", opt->trim.maxLen2, "maximum length keep for read2 after trim tail", true)->check(CLI::Range(0, 1000));
    // polyG tail trimming
    CLI::Option* ptrimG = app.add_flag("-g,--trim_polyg", opt->polyGTrim.enabled, "enable polyG trimming");
    app.add_option("--polyg_min_len", opt->polyGTrim.minLen, "minimum length to detect polyG in the read tail", true)->needs(ptrimG);
    // polyX tail trimming
    CLI::Option* ptrimX = app.add_flag("-x,--trim_polyx", opt->polyXTrim.enabled, "enable polyX trimming");
    app.add_option("--polyx_min_len", opt->polyXTrim.minLen, "minimum length to detect polyG in the read tail", true)->needs(ptrimX);
    // cutting by quality
    CLI::Option* pcutfront = app.add_flag("--cut_front", opt->qualitycut.enableFront, "slide window from 5' to 3', drop the bases in the window if its mean quality < threshold, stop otherwise.");
    CLI::Option* pcuttail = app.add_flag("--cut_tail", opt->qualitycut.enableTail, "slide window from 3' to 5', drop the bases in the window if its mean quality < threshold, stop otherwise.");
    CLI::Option* pcutright = app.add_flag("--cut_right", opt->qualitycut.enableRright, "slide window from 5' to 3', if a window with mean quality < threshold found, drop bases in it and the right part then stop.");
    app.add_option("--cut_window_size", opt->qualitycut.windowSizeShared, "the window size option shared by cut_front, cut_tail or cut_sliding", true)->check(CLI::Range(0, 1000));
    app.add_option("--cut_mean_quality", opt->qualitycut.qualityShared, "the mean quality requirement option shared by cut_front, cut_tail or cut_sliding.", true)->check(CLI::Range(1, 36));
    app.add_option("--cut_front_window_size", opt->qualitycut.windowSizeFront, "the window size option of cut_front", true)->check(CLI::Range(0, 1000))->needs(pcutfront);
    app.add_option("--cut_tail_window_size", opt->qualitycut.windowSizeTail, "the window size option of cut_tail", true)->check(CLI::Range(0, 1000))->needs(pcuttail);
    app.add_option("--cut_right_window_size", opt->qualitycut.windowSizeRight, "the window size option of cut_right", true)->check(CLI::Range(0, 1000))->needs(pcutright);
    app.add_option("--cut_front_mean_quality", opt->qualitycut.qualityFront, "the mean quality option of cut_front", true)->check(CLI::Range(1, 36))->needs(pcutfront);
    app.add_option("--cut_tail_mean_quality", opt->qualitycut.qualityTail, "the mean quality option of cut_tail", true)->check(CLI::Range(1, 36))->needs(pcuttail);
    app.add_option("--cut_right_mean_quality", opt->qualitycut.qualityRight, "the mean quality option of cut_right", true)->check(CLI::Range(1, 36))->needs(pcuttail);
    // quality filtering
    CLI::Option* pqfilter = app.add_flag("--enable_quality_filtering", opt->qualFilter.enabled, "enable quality filtering");
    app.add_option("--qualified_quality_phred", opt->qualFilter.lowQualityLimit, "the minimum quality value that a base is qualified", true)->needs(pqfilter);
    app.add_option("--unqualified_base_limit", opt->qualFilter.lowQualityBaseLimit, "maximum low quality bases allowed in one read", true)->needs(pqfilter);
    app.add_option("--n_base_limit", opt->qualFilter.nBaseLimit, "maximum N bases allowed in one read", true)->needs(pqfilter);
    // length filtering
    CLI::Option* plenfilter = app.add_flag("--enable_length_filter", opt->lengthFilter.enabled, "enable length filter");
    app.add_option("--minimum_length", opt->lengthFilter.minReadLength, "minimum length required for a read", true)->check(CLI::Range(0, 1000))->needs(plenfilter);
    app.add_option("--maximum_length", opt->lengthFilter.maxReadLength, "maximum length allowed for a read", true)->check(CLI::Range(0, 1000))->needs(plenfilter);
    // low complexity filtering
    CLI::Option* plowcfilter = app.add_flag("--enabel_lowcomplexity_filter", opt->complexityFilter.enabled, "enable low complexity filter");
    app.add_option("--minimum_complexity", opt->complexityFilter.threshold, "minimum complexity required for a read", true)->check(CLI::Range(0, 1))->needs(plowcfilter);
    // index filtering
    CLI::Option* pindexfilter = app.add_flag("--filter_by_index", opt->indexFilter.enabled, "enable index filtering");
    app.add_option("--filter_index1", opt->indexFilter.index1File, "file contains a list of barcodes of index1 to be filtered out, one barcode per line")->check(CLI::ExistingFile)->needs(pindexfilter);
    app.add_option("--filter_index2", opt->indexFilter.index2File, "file contains a list of barcodes of index2 to be filtered out, one barcode per line")->check(CLI::ExistingFile)->needs(pindexfilter);
    app.add_option("--filter_index_threshold", opt->indexFilter.threshold, "allowed difference of index barcode for index filtering", true)->check(CLI::Range(0, 10))->needs(pindexfilter);
    // base correction
    app.add_flag("--enable_base_correction", opt->correction.enabled, "enable base correction in overlapped regions of PE reads");
    app.add_option("--overlap_len_required", opt->overlapRequire, "minimum overlapped lenfth needed for overlap analysis based adapter trimming and correction", true)->check(CLI::Range(0, 1000));
    app.add_option("--overlap_diff_limit", opt->overlapDiffLimit, "maximum difference allowed for overlapped region for overlap analysis based adapter trimming and correction", true)->check(CLI::Range(0, 10));
    // umi processing
    CLI::Option* pumi = app.add_flag("--enable_umi_processing", opt->umi.enabled, "enable unique molecular identifier (UMI) preprocessing");
    app.add_option("--umi_loc", opt->umi.location, "0:none, 1:index1, 2:index2, 3:read1, 4:read2, 5:per_index, 6:per_read", true)->check(CLI::Range(1, 6))->needs(pumi);
    app.add_option("--umi_len", opt->umi.length, "if the UMI is in read1/read2, its length should be provided", true)->check(CLI::Range(0, 1000))->needs(pumi);
    app.add_option("--umi_prefix", opt->umi.prefix, "if specified, an underline will be used to connect prefix and UMI (i.e. prefix=UMI)", true)->needs(pumi);
    app.add_option("--umi_skip", opt->umi.skip, "if the UMI is in read1/read2, fastp can skip several bases following UMI", true)->check(CLI::Range(0, 1000))->needs(pumi);
    // overrepresentation sequence analysis
    CLI::Option* pora = app.add_flag("--enable_overrepana", opt->overRepAna.enabled, "enable overrepresented sequence analysis.");
    app.add_option("--overrepana_sampling", opt->overRepAna.sampling, "one in --overrepana_sampling will be computed for overrepresentation analysis", true)->check(CLI::Range(1, 10000))->needs(pora);
    // reporting 
    app.add_option("--json", opt->jsonFile, "the json format report file name", true);
    app.add_option("--html", opt->htmlFile, "the html format report file name", true);
    app.add_option("--title", opt->reportTitle, "report title", true);
    // threading
    app.add_option("--thread", opt->thread, "worker thread number", true)->check(CLI::Range(1, 16));
    // output split
    CLI::Option* split_by_fn = app.add_flag("--split_by_file_number", opt->split.byFileNumber, "split output by limiting total split file number");
    app.add_option("--file_number", opt->split.number, "total split output file number")->needs(split_by_fn);
    CLI::Option* split_by_ln = app.add_flag("--split_by_lines", opt->split.byFileLines, "split output by limiting lines of each file")->excludes(split_by_fn);
    app.add_option("--file_lines", opt->split.size, "split output file line limit")->needs(split_by_ln);
    app.add_option("--split_prefix_digits", opt->digits, "the digits for sequential output", true)->check(CLI::Range(1, 10));
    // parse args
    CLI_PARSE(app, argc, argv);
    // evaluate read length
    Evaluator eva(opt);
    eva.evaluateReadLen();
    // evaluate read number
    eva.evaluateReadNum();
    if(opt->split.byFileNumber){
        opt->split.size = std::max(opt->est.readsNum / opt->split.number, 1);
    }
    if(opt->overRepAna.enabled){
        eva.evaluateOverRepSeqs();
    }
    // evaluate adapter sequence
    if(opt->adapter.enableDetectForPE){
        eva.evaluateAdapterSeq(false);
        eva.evaluateAdapterSeq(true);
    }
    // setup processor
    Processor p(opt);
    p.process();
}