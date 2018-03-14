/* sigalign.cpp 
 * align oxford nanopore event-level signals directly to the reference without
 * basecalling */

#include <iostream>
#include <string>
#include <unordered_map>
#include <math.h>
#include <unistd.h>

#include "fast5.hpp"
#include "arg_parse.hpp"
#include "seed_graph.hpp"
#include "seed_tracker.hpp"
#include "kmer_model.hpp"
#include "timer.h"


std::string FWD_STR = "fwd",
            REV_STR = "rev";

//TODO: relative paths
const std::string MODEL_DEF = "/home-4/skovaka1@jhu.edu/code/nanopore_aligner/kmer_models/r9.2_180mv_250bps_6mer/template_median68pA.model";

bool get_events(std::ostream &err, std::string filename, std::vector<Event> &events);

enum Opt {MODEL       = 'm',
          REFERENCE   = 'r',
          INDEX_PREFIX   = 'f',
          READ_LIST   = 'l',
          OUT_PREFIX     = 'o',

          TALLY_DIST  = 't',

          MIN_SEED_LEN = 's',
          ANCHOR_LEN  = 'a',
          MAX_IGNORES = 'i',
          MAX_SKIPS   = 'k',
          STAY_FRAC   = 'y',

          EXTEND_EVPR = 'E',
          ANCHOR_EVPR = 'A',
          SEED_PR     = 'S',
          STAY_PR     = 'Y',
          
          READ_UNTIL = 'u'};

int main(int argc, char** argv) {
    ArgParse args("UNCALLED: Utility for Nanopore Current Alignment to Large Expanses of DNA");

    args.add_string(Opt::MODEL, "model", "/home-4/skovaka1@jhu.edu/code/nanopore_aligner/kmer_models/r9.2_180mv_250bps_6mer/template_median68pA.model", "Nanopore kmer model");
    args.add_string(Opt::REFERENCE,   "reference",    "",     "");
    args.add_string(Opt::READ_LIST,   "read_list",    "",     "");
    args.add_string(Opt::INDEX_PREFIX,  "index_prefix",   "",     "");
    args.add_string(Opt::OUT_PREFIX,  "out_prefix",   "./",     "");
    args.add_string(Opt::EXTEND_EVPR, "extend_evprs", "1-10.0_5-4.0_100-2.4",  "");
    args.add_int(   Opt::TALLY_DIST,  "tally_dist",   128,     "");
    args.add_int(   Opt::MIN_SEED_LEN,"min_seed_len", 15,     "");
    args.add_int(   Opt::ANCHOR_LEN,  "anchor_len",   12,     "");
    args.add_int(   Opt::MAX_IGNORES, "max_ignores",  0,    "");
    args.add_int(   Opt::MAX_SKIPS,   "max_skips",    0,    "");
    args.add_int(   Opt::READ_UNTIL,  "read_until",   0, "");
    args.add_double(Opt::STAY_FRAC,   "stay_frac",    0.5,    "");
    args.add_double(Opt::ANCHOR_EVPR, "anchor_evpr", -2.4,    "");
    //args.add_double(Opt::EXTEND_EVPR, "extend_evpr", -10,  "");
    args.add_double(Opt::SEED_PR,     "seed_pr",     -3.75,  "");
    args.add_double(Opt::STAY_PR,     "stay_pr",     -8.343, "");

    args.parse_args(argc, argv);

    std::string prefix = args.get_string(Opt::OUT_PREFIX) + args.get_param_str();
    std::ofstream seeds_out(prefix + "_seeds.txt");
    std::ofstream alns_out(prefix + "_aln.txt");
    std::ofstream err_out(prefix + "_err.txt");

    std::cerr << "Writing:" << "\n"
              << prefix << "_seeds.txt" << "\n"
              << prefix << "_aln.txt" << "\n"
              << prefix << "_err.txt" << "\n";

    err_out << "Loading model\n";
    KmerModel model(args.get_string(Opt::MODEL));


    BaseFMI fwd_fmi, rev_fmi;

    if (!args.get_string(Opt::INDEX_PREFIX).empty()) {
        std::string p = args.get_string(Opt::INDEX_PREFIX);
        std::ifstream fwd_in(p + "fwdFM.txt"),
                      rev_in(p + "revFM.txt");

        std::cerr << "Reading forward FMI\n";
        fwd_fmi = BaseFMI(fwd_in, args.get_int(Opt::TALLY_DIST));

        std::cerr << "Reading reverse FMI\n";
        rev_fmi = BaseFMI(rev_in, args.get_int(Opt::TALLY_DIST));

    } else {
        std::cerr << "Parsing fasta\n";
        std::ifstream ref_file(args.get_string(Opt::REFERENCE));
        std::string fwd_bases, rev_bases;
        model.parse_fasta(ref_file, fwd_bases, rev_bases);

        std::cerr << "Building forward FMI\n";
        fwd_fmi = BaseFMI(fwd_bases, args.get_int(Opt::TALLY_DIST));

        std::cerr << "Building reverse FMI\n";
        rev_fmi = BaseFMI(rev_bases, args.get_int(Opt::TALLY_DIST));
    }
    std::cerr << "Done\n";

    //100-2.4_5-4.0_1-10.0
    std::string expr_str = args.get_string(Opt::EXTEND_EVPR);
    size_t ex_i = 0, ex_j = expr_str.find('_'), ex_k;
    std::vector<int> expr_lengths;
    std::vector<double> expr_probs;
    while(ex_i < expr_str.size()) {
        ex_k = expr_str.find('-', ex_i);
        expr_lengths.push_back( atoi(expr_str.substr(ex_i, ex_k).c_str()) );
        expr_probs.push_back( atof(expr_str.substr(ex_k, ex_j).c_str()) );

        ex_i = ex_j+1;
        ex_j = expr_str.find('_', ex_i+1);
        if (ex_j == std::string::npos) {
            ex_j = expr_str.size();
        }
    }


    AlnParams aln_params(model,
                         args.get_int(Opt::MIN_SEED_LEN),
                         args.get_int(Opt::ANCHOR_LEN),
                         args.get_int(Opt::MAX_IGNORES),
                         args.get_int(Opt::MAX_SKIPS),
                         args.get_double(Opt::STAY_FRAC),
                         args.get_double(Opt::ANCHOR_EVPR),
                         //args.get_double(Opt::EXTEND_EVPR),
                         expr_lengths,
                         expr_probs,
                         args.get_double(Opt::SEED_PR),
                         args.get_double(Opt::STAY_PR));

    SeedGraph rev_sg(rev_fmi, aln_params, "rev"),
              fwd_sg(fwd_fmi, aln_params, "fwd");

    std::ifstream reads_file(args.get_string(Opt::READ_LIST));

    if (!reads_file) {
        err_out << "Error: couldn't open '" 
                  << args.get_string(Opt::READ_LIST) << "'\n";
        return 1;
    }

    unsigned int ru_min_revts = args.get_int(Opt::READ_UNTIL);
    bool read_until = ru_min_revts > 0;

    std::string read_line, read_filename;


    while (getline(reads_file, read_line)) {

        unsigned int aln_st = 0, aln_en = 0;

        unsigned int i = read_line.find("\t");

        if (i == std::string::npos) {
            read_filename = read_line;
        } else {
            read_filename = read_line.substr(0, i);

            unsigned int j = read_line.find("\t", i+1);

            if (j == std::string::npos) {
                aln_st = atoi(read_line.substr(i+1).c_str());
            } else {
                aln_st = atoi(read_line.substr(i+1, j).c_str());
                aln_en = atoi(read_line.substr(j+1).c_str());
            }
        }
            

        std::vector<Event> events;
        
        if (!get_events(err_out, read_filename, events)) {
            continue;
        }

        NormParams norm = model.get_norm_params(events);
        model.normalize_events(events, norm);

        if (aln_en == aln_st) {
            aln_en = events.size() - 1;
        }

        //NormParams norm = model.get_norm_params(events);


        unsigned int aln_len = aln_en - aln_st + 1;

        fwd_sg.new_read(aln_len);
        rev_sg.new_read(aln_len);

        SeedTracker fwd_tracker, rev_tracker;

        Timer timer;
        
        seeds_out << "== " << read_filename << " ==\n";
        alns_out << "== " << read_filename << " ==\n";
        alns_out << "== scale/shift: " << norm.scale << " " << norm.shift << " ==\n";
        alns_out << "== aligning " 
                 << aln_st << "-" 
                 << aln_en << " of " 
                 << events.size() << " events ==\n";

        err_out << read_filename << "\n";

        unsigned int status_step = aln_len / 10,
            status = 0;

        unsigned int e = aln_en;
        for (; e >= aln_st && e <= aln_en; e--) {
            std::vector<Result> fwd_seeds = fwd_sg.add_event(events[e], seeds_out),
                                rev_seeds = rev_sg.add_event(events[e], seeds_out);

            ReadAln fa = fwd_tracker.add_seeds(fwd_seeds);
            ReadAln ra = rev_tracker.add_seeds(rev_seeds);

            if (read_until && max(fa.total_len_, ra.total_len_) > ru_min_revts) {
                //alns_out << fa.total_len_ << "\t" << ra.total_len_ << "\n";
                ReadAln a = fa;
                if (fa.total_len_ < ra.total_len_) {
                    a = ra;
                }
                
                if(fwd_tracker.check_ratio(a, 2) && rev_tracker.check_ratio(a, 2)) {
                    //std::cout << "DONE\n";
                    break;
                }
            }
            

            if (status == status_step) {
                unsigned int prog = (unsigned int) ((100.0 * (aln_len - e)) / aln_len);
                err_out << prog << "%  (" << timer.get() / 1000 << ")\n";
                status = 0;
                err_out.flush();
                seeds_out.flush();
                alns_out.flush();
            } else {
                status++;
            }
        }

        err_out << "100% (" << timer.get() / 1000 << ")\n";

        fwd_tracker.print(alns_out, FWD_STR, (size_t) 5);
        rev_tracker.print(alns_out, REV_STR, (size_t) 5);

        err_out.flush();
        seeds_out.flush();
        alns_out.flush();

        alns_out  << "== " << timer.lap() << " ms, " 
                  << (aln_en - e + 1) << " events ==\n";

        seeds_out << "== " << timer.lap() / 1000 << " sec ==\n";
    }
}

bool get_events(std::ostream &err, std::string filename, std::vector<Event> &events) {
    events.clear();

    if (!fast5::File::is_valid_file(filename)) {
        err << "Error: '" << filename << "' is not a valid file \n";
    }

    try {
        fast5::File file;
        file.open(filename);
        
        if (!file.is_open()) {  
            err << "Error: unable to open '" 
                      << filename << "'\n";

        } else if (!file.have_eventdetection_events()) {
            err << "Error: file '" << filename
                      << "' does not contain events\n";

        } else {
            events = file.get_eventdetection_events();
            return true;
        }

    } catch (hdf5_tools::Exception& e) {
        err << "Error: hdf5 exception '" << e.what() << "'\n";
    }

    return false;
}
