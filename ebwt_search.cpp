#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <getopt.h>
#include <pthread.h>
#include <math.h>
#include <utility>
#include "alphabet.h"
#include "assert_helpers.h"
#include "endian_swap.h"
#include "ebwt.h"
#include "formats.h"
#include "sequence_io.h"
#include "tokenize.h"
#include "aln_sink.h"
#include "pat.h"
#include "bitset.h"
#include "threading.h"
#include "ds.h"
#include "aligner_metrics.h"
#include "sam.h"
#include "aligner_seed.h"
#include "aligner_seed_policy.h"
#include "aligner_sw.h"
#include "aligner_sw_driver.h"
#include "aligner_counters.h"
#include "aligner_cache.h"
#include "util.h"
#include "pe.h"
#include "simple_func.h"
#include "presets.h"

using namespace std;

static EList<string> mates1;  // mated reads (first mate)
static EList<string> mates2;  // mated reads (second mate)
static EList<string> mates12; // mated reads (1st/2nd interleaved in 1 file)
static string adjIdxBase;
int gVerbose;      // be talkative
static bool startVerbose; // be talkative at startup
int gQuiet;        // print nothing but the alignments
static int sanityCheck;   // enable expensive sanity checks
static int format;        // default read format is FASTQ
static string origString; // reference text, or filename(s)
static int seed;          // srandom() seed
static int timing;        // whether to report basic timing data
static int metricsIval;   // interval between alignment metrics messages (0 = no messages)
static string metricsFile;// output file to put alignment metrics in
static bool metricsStderr;// output file to put alignment metrics in
static bool allHits;      // for multihits, report just one
static int showVersion;   // just print version and quit?
static int ipause;        // pause before maching?
static uint32_t qUpto;    // max # of queries to read
int gTrim5;         // amount to trim from 5' end
int gTrim3;         // amount to trim from 3' end
static int offRate;       // keep default offRate
static char *patDumpfile; // filename to dump patterns to
static bool solexaQuals;  // quality strings are solexa quals, not phred, and subtract 64 (not 33)
static bool phred64Quals; // quality chars are phred, but must subtract 64 (not 33)
static bool integerQuals; // quality strings are space-separated strings of integers, not ASCII
static int nthreads;      // number of pthreads operating concurrently
static int outType;  // style of output
static bool noRefNames;       // true -> print reference indexes; not names
static uint32_t khits;  // number of hits per read; >1 is much slower
static uint32_t mhits;  // don't report any hits if there are > mhits
static int partitionSz; // output a partitioning key in first field
bool gNoMaqRound; // true -> don't round quals to nearest 10 like maq
static bool useSpinlock;  // false -> don't use of spinlocks even if they're #defines
static bool fileParallel; // separate threads read separate input files in parallel
static bool useShmem;     // use shared memory to hold the index
static bool useMm;        // use memory-mapped files to hold the index
static bool mmSweep;      // sweep through memory-mapped files immediately after mapping
int gMinInsert;     // minimum insert size
int gMaxInsert;     // maximum insert size
bool gMate1fw;           // -1 mate aligns in fw orientation on fw strand
bool gMate2fw;           // -2 mate aligns in rc orientation on fw strand
bool mateFwSet;
bool gFlippedMatesOK;  // allow mates to be in wrong order
bool gDovetailMatesOK; // allow one mate to extend off the end of the other
bool gContainMatesOK;  // allow one mate to contain the other in PE alignment
bool gOlapMatesOK;     // allow mates to overlap in PE alignment
bool gExpandToFrag;    // incr max frag length to =larger mate len if necessary
bool gReportDiscordant; // find and report discordant paired-end alignments
bool gReportMixed;      // find and report unpaired alignments for paired reads
static uint32_t cacheLimit;      // ranges w/ size > limit will be cached
static uint32_t cacheSize;       // # words per range cache
static uint32_t skipReads;       // # reads/read pairs to skip
bool gNofw; // don't align fw orientation of read
bool gNorc; // don't align rc orientation of read
static uint32_t fastaContLen;
static uint32_t fastaContFreq;
static bool hadoopOut; // print Hadoop status and summary messages
static bool fuzzy;
static bool fullRef;
static bool samTruncQname; // whether to truncate QNAME to 255 chars
static bool samOmitSecSeqQual; // omit SEQ/QUAL for 2ndary alignments?
static bool samNoHead; // don't print any header lines in SAM output
static bool samNoSQ;   // don't print @SQ header lines
static bool sam_print_as;
static bool sam_print_xs;
static bool sam_print_xn;
static bool sam_print_cs;
static bool sam_print_cq;
static bool sam_print_x0;
static bool sam_print_x1;
static bool sam_print_xm;
static bool sam_print_xo;
static bool sam_print_xg;
static bool sam_print_nm;
static bool sam_print_md;
static bool sam_print_yf;
static bool sam_print_yi;
static bool sam_print_ym;
static bool sam_print_yp;
static bool sam_print_yt;
static bool sam_print_ys;
static bool bwaSwLike;
static float bwaSwLikeC;
static float bwaSwLikeT;
static float mapqTopCoeff;
static float mapqBotCoeff;
static float mapqMax;             // maximum Mapq 
static bool qcFilter;
static bool sortByScore;   // prioritize alignments to report by score?
bool gColor;     // true -> inputs are colorspace
bool gColorExEnds; // true -> nucleotides on either end of decoded cspace alignment should be excluded
bool gReportOverhangs; // false -> filter out alignments that fall off the end of a reference sequence
static string rgid; // ID: setting for @RG header line
static string rgs;  // SAM outputs for @RG header line
static string rgs_optflag; // SAM optional flag to add corresponding to @RG ID
int gSnpPhred; // probability of SNP, for scoring colorspace alignments
static EList<bool> suppressOuts; // output fields to suppress
static bool msample; // whether to report a random alignment when maxed-out via -m/-M
bool gColorSeq; // true -> show colorspace alignments as colors, not decoded bases
bool gColorEdit; // true -> show edits as colors, not decoded bases
bool gColorQual; // true -> show colorspace qualities as original quals, not decoded quals
static bool printPlaceholders; // true -> print records for maxed-out, unaligned reads
static bool printFlags; // true -> print alignment flags
static bool printParams; // true -> print parameters like seed spacing, cost ceiling
int      gGapBarrier; // # diags on top/bot only to be entered diagonally
int64_t  gRowLow;     // backtraces start from row w/ idx >= this (-1=no limit)
bool     gRowFirst;   // sort alignments by row then score?
static EList<string> qualities;
static EList<string> qualities1;
static EList<string> qualities2;
static string polstr; // temporary holder for policy string
static bool  msNoCache;      // true -> disable local cache
static int   bonusMatchType; // how to reward matches
static int   bonusMatch;     // constant reward if bonusMatchType=constant
static int   penMmcType;     // how to penalize mismatches
static int   penMmc;         // constant if mm penMmcType=constant
static int   penSnp;         // penalty for nucleotide mismatches in decoded colorspace als
static int   penNType;       // how to penalize Ns in the read
static int   penN;           // constant if N pelanty is a constant
static bool  penNCatPair;    // concatenate mates before N filtering?
static bool  localAlign;     // do local alignment in DP steps
static bool  noisyHpolymer;  // set to true if gap penalties should be reduced to be consistent with a sequencer that under- and overcalls homopolymers
static int   penRdGapConst;   // constant cost of extending a gap in the read
static int   penRfGapConst;   // constant cost of extending a gap in the reference
static int   penRdGapLinear;  // coeff of linear term for cost of gap extension in read
static int   penRfGapLinear;  // coeff of linear term for cost of gap extension in ref
static SimpleFunc scoreMin;    // minimum valid score as function of read len
static SimpleFunc scoreFloor;  // 
static SimpleFunc nCeil;      // max # Ns allowed as function of read len
static SimpleFunc msIval;     // interval between seeds as function of read len
static int   multiseedMms;   // mismatches permitted in a multiseed seed
static int   multiseedLen;   // length of multiseed seeds
static string saCountersFn;  // filename to dump per-read SeedAligner counters to
static string saActionsFn;   // filename to dump all alignment actions to
static string saHitsFn;      // filename to dump all seed hits to
static uint32_t seedCacheLocalMB;   // # MB to use for non-shared seed alignment cacheing
static uint32_t seedCacheCurrentMB; // # MB to use for current-read seed hit cacheing
static SimpleFunc posfrac;   // number of seed poss to try as function of # poss
static SimpleFunc rowmult;   // number of hits per pos to try as function of # hits
static size_t maxhalf;       // max width on one side of DP table
static bool seedSummaryOnly; // print summary information about seed hits, not alignments
static bool scanNarrowed;    // true -> do ref scan even when seed is narrow
static bool noSse;           // disable SSE-based dynamic programming
static string defaultPreset; // default preset; applied immediately

#define DMAX std::numeric_limits<double>::max()

static void resetOptions() {
	mates1.clear();
	mates2.clear();
	mates12.clear();
	adjIdxBase	            = "";
	gVerbose                = 0;
	startVerbose			= 0;
	gQuiet					= false;
	sanityCheck				= 0;  // enable expensive sanity checks
	format					= FASTQ; // default read format is FASTQ
	origString				= ""; // reference text, or filename(s)
	seed					= 0; // srandom() seed
	timing					= 0; // whether to report basic timing data
	metricsIval				= 0; // interval between alignment metrics messages (0 = no messages)
	metricsFile             = ""; // output file to put alignment metrics in
	metricsStderr           = false; // print metrics to stderr (in addition to --metrics-file if it's specified
	allHits					= false; // for multihits, report just one
	showVersion				= 0; // just print version and quit?
	ipause					= 0; // pause before maching?
	qUpto					= 0xffffffff; // max # of queries to read
	gTrim5					= 0; // amount to trim from 5' end
	gTrim3					= 0; // amount to trim from 3' end
	offRate					= -1; // keep default offRate
	patDumpfile				= NULL; // filename to dump patterns to
	solexaQuals				= false; // quality strings are solexa quals, not phred, and subtract 64 (not 33)
	phred64Quals			= false; // quality chars are phred, but must subtract 64 (not 33)
	integerQuals			= false; // quality strings are space-separated strings of integers, not ASCII
	nthreads				= 1;     // number of pthreads operating concurrently
	outType					= OUTPUT_SAM;  // style of output
	noRefNames				= false; // true -> print reference indexes; not names
	khits					= 1;     // number of hits per read; >1 is much slower
	mhits					= 1;     // don't report any hits if there are > mhits
	partitionSz				= 0;     // output a partitioning key in first field
	gNoMaqRound				= false; // true -> don't round quals to nearest 10 like maq
	useSpinlock				= true;  // false -> don't use of spinlocks even if they're #defines
	fileParallel			= false; // separate threads read separate input files in parallel
	useShmem				= false; // use shared memory to hold the index
	useMm					= false; // use memory-mapped files to hold the index
	mmSweep					= false; // sweep through memory-mapped files immediately after mapping
	gMinInsert				= 0;     // minimum insert size
	gMaxInsert				= 450;   // maximum insert size
	gMate1fw				= true;  // -1 mate aligns in fw orientation on fw strand
	gMate2fw				= false; // -2 mate aligns in rc orientation on fw strand
	mateFwSet				= false; // true -> user set mate1fw/mate2fw with --ff/--fr/--rf
	gFlippedMatesOK         = false; // allow mates to be in wrong order
	gDovetailMatesOK        = true;  // allow one mate to extend off the end of the other
	gContainMatesOK         = true;  // allow one mate to contain the other in PE alignment
	gOlapMatesOK            = true;  // allow mates to overlap in PE alignment
	gExpandToFrag           = true;  // incr max frag length to =larger mate len if necessary
	gReportDiscordant       = true;  // find and report discordant paired-end alignments
	gReportMixed            = true;  // find and report unpaired alignments for paired reads

	cacheLimit				= 5;     // ranges w/ size > limit will be cached
	cacheSize				= 0;     // # words per range cache
	skipReads				= 0;     // # reads/read pairs to skip
	gNofw					= false; // don't align fw orientation of read
	gNorc					= false; // don't align rc orientation of read
	fastaContLen			= 0;
	fastaContFreq			= 0;
	hadoopOut				= false; // print Hadoop status and summary messages
	fuzzy					= false; // reads will have alternate basecalls w/ qualities
	fullRef					= false; // print entire reference name instead of just up to 1st space
	samTruncQname           = true;  // whether to truncate QNAME to 255 chars
	samOmitSecSeqQual       = false; // omit SEQ/QUAL for 2ndary alignments?
	samNoHead				= false; // don't print any header lines in SAM output
	samNoSQ					= false; // don't print @SQ header lines
	sam_print_as            = true;
	sam_print_xs            = true;
	sam_print_xn            = true;
	sam_print_cs            = false;
	sam_print_cq            = false;
	sam_print_x0            = true;
	sam_print_x1            = true;
	sam_print_xm            = true;
	sam_print_xo            = true;
	sam_print_xg            = true;
	sam_print_nm            = true;
	sam_print_md            = true;
	sam_print_yf            = true;
	sam_print_yi            = false;
	sam_print_ym            = true;
	sam_print_yp            = true;
	sam_print_yt            = true;
	sam_print_ys            = true;
	bwaSwLike               = false;
	bwaSwLikeC              = 5.5f;
	bwaSwLikeT              = 20.0f;
	mapqTopCoeff            = 0.6f;
	mapqBotCoeff            = 0.9f;
	mapqMax                 = 40.0f; // maximum Mapq 
	qcFilter                = false; // don't believe upstream qc by default
	sortByScore             = true;  // prioritize alignments to report by score?
	gColor					= false; // don't align in colorspace by default
	gColorExEnds			= true;  // true -> nucleotides on either end of decoded cspace alignment should be excluded
	gReportOverhangs        = false; // false -> filter out alignments that fall off the end of a reference sequence
	rgid					= "";    // SAM outputs for @RG header line
	rgs						= "";    // SAM outputs for @RG header line
	rgs_optflag				= "";    // SAM optional flag to add corresponding to @RG ID
	gSnpPhred				= 30;    // probability of SNP, for scoring colorspace alignments
	suppressOuts.clear();            // output fields to suppress
	suppressOuts.resize(64);
	suppressOuts.fill(false);
	msample				    = true;
	gColorSeq				= false; // true -> show colorspace alignments as colors, not decoded bases
	gColorEdit				= false; // true -> show edits as colors, not decoded bases
	gColorQual				= false; // true -> show colorspace qualities as original quals, not decoded quals
	printPlaceholders       = true;  // true -> print records for maxed-out, unaligned reads
	printFlags              = true;  // true -> print alignment flags
	printParams				= false; // true -> print parameters regarding seeding, ceilings
	gGapBarrier				= 4;     // disallow gaps within this many chars of either end of alignment
	gRowLow                 = -1;    // backtraces start from row w/ idx >= this (-1=no limit)
	gRowFirst               = false; // sort alignments by row then score?
	qualities.clear();
	qualities1.clear();
	qualities2.clear();
	polstr.clear();
	msNoCache       = true; // true -> disable local cache
	bonusMatchType  = DEFAULT_MATCH_BONUS_TYPE;
	bonusMatch      = DEFAULT_MATCH_BONUS;
	penMmcType      = DEFAULT_MM_PENALTY_TYPE;
	penMmc          = DEFAULT_MM_PENALTY;
	penSnp          = DEFAULT_SNP_PENALTY;
	penNType        = DEFAULT_N_PENALTY_TYPE;
	penN            = DEFAULT_N_PENALTY;
	penNCatPair     = DEFAULT_N_CAT_PAIR; // concatenate mates before N filtering?
	localAlign      = false;     // do local alignment in DP steps
	noisyHpolymer   = false;
	penRdGapConst   = DEFAULT_READ_GAP_CONST;
	penRfGapConst   = DEFAULT_REF_GAP_CONST;
	penRdGapLinear  = DEFAULT_READ_GAP_LINEAR;
	penRfGapLinear  = DEFAULT_REF_GAP_LINEAR;
	scoreMin.init  (SIMPLE_FUNC_LINEAR, DEFAULT_MIN_CONST,   DEFAULT_MIN_LINEAR);
	scoreFloor.init(SIMPLE_FUNC_LINEAR, DEFAULT_FLOOR_CONST, DEFAULT_FLOOR_LINEAR);
	nCeil.init    (SIMPLE_FUNC_LINEAR, 0.0f, DMAX, 2.0f, 0.1f);
	msIval.init   (SIMPLE_FUNC_LINEAR, 1.0f, DMAX, DEFAULT_IVAL_B, DEFAULT_IVAL_A);
	posfrac.init  (SIMPLE_FUNC_LINEAR, 1.0f, DMAX, DEFAULT_POSMIN, DEFAULT_POSFRAC);
	rowmult.init  (SIMPLE_FUNC_CONST,  1.0f, DMAX, DEFAULT_ROWMULT, 0.0f);
	multiseedMms    = DEFAULT_SEEDMMS;
	multiseedLen    = DEFAULT_SEEDLEN;
	saCountersFn.clear();    // filename to dump per-read SeedAligner counters to
	saActionsFn.clear();     // filename to dump all alignment actions to
	saHitsFn.clear();        // filename to dump all seed hits to
	seedCacheLocalMB   = 32; // # MB to use for non-shared seed alignment cacheing
	seedCacheCurrentMB = 16; // # MB to use for current-read seed hit cacheing
	maxhalf            = 15; // max width on one side of DP table
	seedSummaryOnly    = false; // print summary information about seed hits, not alignments
	scanNarrowed       = false; // true -> do ref scan even when seed is narrow
	noSse              = false; // disable SSE-based dynamic programming
	defaultPreset      = "fast%LOCAL%"; // default preset; applied immediately
}

static const char *short_options = "fF:qbzhcu:rv:s:aP:t3:5:o:w:p:k:M:1:2:I:X:CQ:";

enum {
	ARG_ORIG = 256,
	ARG_SEED,
	ARG_DUMP_PATS,
	ARG_SOLEXA_QUALS,
	ARG_VERBOSE,
	ARG_STARTVERBOSE,
	ARG_QUIET,
	ARG_FAST,
	ARG_METRIC_IVAL,
	ARG_METRIC_FILE,
	ARG_METRIC_STDERR,
	ARG_REFIDX,
	ARG_SANITY,
	ARG_PARTITION,
	ARG_INTEGER_QUALS,
	ARG_NOMAQROUND,
	ARG_USE_SPINLOCK,
	ARG_FILEPAR,
	ARG_SHMEM,
	ARG_MM,
	ARG_MMSWEEP,
	ARG_FF,
	ARG_FR,
	ARG_RF,
	ARG_NO_MIXED,
	ARG_NO_DISCORDANT,
	ARG_CACHE_LIM,
	ARG_CACHE_SZ,
	ARG_NO_FW,
	ARG_NO_RC,
	ARG_SKIP,
	ARG_ONETWO,
	ARG_PHRED64,
	ARG_PHRED33,
	ARG_HADOOPOUT,              // --hadoopout
	ARG_FUZZY,                  // --fuzzy
	ARG_FULLREF,                // --fullref
	ARG_USAGE,                  // --usage
	ARG_SNPPHRED,               // --snpphred
	ARG_SNPFRAC,                // --snpfrac
	ARG_SAM_NO_QNAME_TRUNC,     // --sam-no-qname-trunc
	ARG_SAM_OMIT_SEC_SEQ,       // --sam-omit-sec-seq
	ARG_SAM_NOHEAD,             // --sam-noHD/--sam-nohead
	ARG_SAM_NOSQ,               // --sam-nosq/--sam-noSQ
	ARG_SAM_RG,                 // --sam-RG
	ARG_SUPPRESS_FIELDS,        // --suppress
	ARG_COLOR_SEQ,              // --col-cseq
	ARG_COLOR_EDIT,             // --col-cedit
	ARG_COLOR_QUAL,             // --col-cqual
	ARG_PRINT_PLACEHOLDERS,     // --print-placeholders
	ARG_PRINT_FLAGS,            // --print-flags
	ARG_PRINT_PARAMS,           // --print-params
	ARG_COLOR_KEEP_ENDS,        // --col-keepends
	ARG_GAP_BAR,                // --gbar
	ARG_QUALS1,                 // --Q1
	ARG_QUALS2,                 // --Q2
	ARG_QSEQ,                   // --qseq
	ARG_SEED_SUMM,              // --seed-summary
	ARG_OVERHANG,               // --overhang
	ARG_NO_CACHE,               // --no-cache
	ARG_USE_CACHE,              // --cache
	ARG_NOISY_HPOLY,            // --454/--ion-torrent
	ARG_LOCAL,                  // --local
	ARG_SCAN_NARROWED,          // --scan-narrowed
	ARG_NO_SSE,                 // --no-sse
	ARG_QC_FILTER,              // --qc-filter
	ARG_BWA_SW_LIKE,            // --bwa-sw-like
	ARG_MULTISEED_IVAL,         // --multiseed
	ARG_MULTISEED_IVAL_CONST,   // --multiseed-const
	ARG_MULTISEED_IVAL_LINEAR,  // --multiseed-linear
	ARG_MULTISEED_IVAL_SQRT,    // --multiseed-sqrt
	ARG_MULTISEED_IVAL_LOG,     // --multiseed-log
	ARG_SCORE_MIN_LINEAR,       // --score-min
	ARG_SCORE_MIN_CONST,        // --score-min-const
	ARG_SCORE_MIN_SQRT,         // --score-min-sqrt
	ARG_SCORE_MIN_LOG,          // --score-min-log
	ARG_SCORES,                 // --scoring
	ARG_N_CEIL,                 // --n-ceil
	ARG_DPAD,                   // --dpad
	ARG_MAPQ_TOP_COEFF,         // --mapq-top-coeff
	ARG_MAPQ_BOT_COEFF,         // --mapq-bot-coeff
	ARG_MAPQ_MAX,               // --mapq-max
	ARG_SAM_PRINT_YI,           // --mapq-print-inputs
	ARG_ALIGN_POLICY,           // --policy
	ARG_PRESET_VERY_FAST,       // --very-fast
	ARG_PRESET_FAST,            // --fast
	ARG_PRESET_SENSITIVE,       // --sensitive
	ARG_PRESET_VERY_SENSITIVE,  // --very-sensitive
	ARG_NO_SCORE_PRIORITY       // --no-score-priority
};


static struct option long_options[] = {
	{(char*)"verbose",      no_argument,       0,            ARG_VERBOSE},
	{(char*)"startverbose", no_argument,       0,            ARG_STARTVERBOSE},
	{(char*)"quiet",        no_argument,       0,            ARG_QUIET},
	{(char*)"sanity",       no_argument,       0,            ARG_SANITY},
	{(char*)"pause",        no_argument,       &ipause,      1},
	{(char*)"orig",         required_argument, 0,            ARG_ORIG},
	{(char*)"all",          no_argument,       0,            'a'},
	{(char*)"solexa-quals", no_argument,       0,            ARG_SOLEXA_QUALS},
	{(char*)"integer-quals",no_argument,       0,            ARG_INTEGER_QUALS},
	{(char*)"metrics",      required_argument, 0,            ARG_METRIC_IVAL},
	{(char*)"metrics-file", required_argument, 0,            ARG_METRIC_FILE},
	{(char*)"metrics-stderr",no_argument,      0,            ARG_METRIC_STDERR},
	{(char*)"time",         no_argument,       0,            't'},
	{(char*)"trim3",        required_argument, 0,            '3'},
	{(char*)"trim5",        required_argument, 0,            '5'},
	{(char*)"seed",         required_argument, 0,            ARG_SEED},
	{(char*)"qupto",        required_argument, 0,            'u'},
	{(char*)"offrate",      required_argument, 0,            'o'},
	{(char*)"version",      no_argument,       &showVersion, 1},
	{(char*)"dumppats",     required_argument, 0,            ARG_DUMP_PATS},
	{(char*)"filepar",      no_argument,       0,            ARG_FILEPAR},
	{(char*)"help",         no_argument,       0,            'h'},
	{(char*)"threads",      required_argument, 0,            'p'},
	{(char*)"khits",        required_argument, 0,            'k'},
	{(char*)"minins",       required_argument, 0,            'I'},
	{(char*)"maxins",       required_argument, 0,            'X'},
	{(char*)"quals",        required_argument, 0,            'Q'},
	{(char*)"Q1",           required_argument, 0,            ARG_QUALS1},
	{(char*)"Q2",           required_argument, 0,            ARG_QUALS2},
	{(char*)"nomaqround",   no_argument,       0,            ARG_NOMAQROUND},
	{(char*)"refidx",       no_argument,       0,            ARG_REFIDX},
	{(char*)"partition",    required_argument, 0,            ARG_PARTITION},
	{(char*)"nospin",       no_argument,       0,            ARG_USE_SPINLOCK},
	{(char*)"ff",           no_argument,       0,            ARG_FF},
	{(char*)"fr",           no_argument,       0,            ARG_FR},
	{(char*)"rf",           no_argument,       0,            ARG_RF},
	{(char*)"mixthresh",    required_argument, 0,            'x'},
	{(char*)"cachelim",     required_argument, 0,            ARG_CACHE_LIM},
	{(char*)"cachesz",      required_argument, 0,            ARG_CACHE_SZ},
	{(char*)"nofw",         no_argument,       0,            ARG_NO_FW},
	{(char*)"norc",         no_argument,       0,            ARG_NO_RC},
	{(char*)"skip",         required_argument, 0,            's'},
	{(char*)"12",           required_argument, 0,            ARG_ONETWO},
	{(char*)"phred33-quals", no_argument,      0,            ARG_PHRED33},
	{(char*)"phred64-quals", no_argument,      0,            ARG_PHRED64},
	{(char*)"solexa1.3-quals", no_argument,    0,            ARG_PHRED64},
	{(char*)"mm",           no_argument,       0,            ARG_MM},
	{(char*)"shmem",        no_argument,       0,            ARG_SHMEM},
	{(char*)"mmsweep",      no_argument,       0,            ARG_MMSWEEP},
	{(char*)"hadoopout",    no_argument,       0,            ARG_HADOOPOUT},
	{(char*)"fuzzy",        no_argument,       0,            ARG_FUZZY},
	{(char*)"fullref",      no_argument,       0,            ARG_FULLREF},
	{(char*)"usage",        no_argument,       0,            ARG_USAGE},
	{(char*)"gaps",         no_argument,       0,            'g'},
	{(char*)"sam-no-qname-trunc", no_argument, 0,            ARG_SAM_NO_QNAME_TRUNC},
	{(char*)"sam-omit-sec-seq", no_argument,   0,            ARG_SAM_OMIT_SEC_SEQ},
	{(char*)"sam-nohead",   no_argument,       0,            ARG_SAM_NOHEAD},
	{(char*)"sam-noHD",     no_argument,       0,            ARG_SAM_NOHEAD},
	{(char*)"sam-nosq",     no_argument,       0,            ARG_SAM_NOSQ},
	{(char*)"sam-noSQ",     no_argument,       0,            ARG_SAM_NOSQ},
	{(char*)"color",        no_argument,       0,            'C'},
	{(char*)"sam-RG",       required_argument, 0,            ARG_SAM_RG},
	{(char*)"snpphred",     required_argument, 0,            ARG_SNPPHRED},
	{(char*)"snpfrac",      required_argument, 0,            ARG_SNPFRAC},
	{(char*)"suppress",     required_argument, 0,            ARG_SUPPRESS_FIELDS},
	{(char*)"col-cseq",     no_argument,       0,            ARG_COLOR_SEQ},
	{(char*)"col-cqual",    no_argument,       0,            ARG_COLOR_QUAL},
	{(char*)"col-cedit",    no_argument,       0,            ARG_COLOR_EDIT},
	{(char*)"col-keepends", no_argument,       0,            ARG_COLOR_KEEP_ENDS},
	{(char*)"print-placeholders", no_argument, 0,            ARG_PRINT_PLACEHOLDERS},
	{(char*)"print-flags",  no_argument,       0,            ARG_PRINT_FLAGS},
	{(char*)"print-params", no_argument,       0,            ARG_PRINT_PARAMS},
	{(char*)"gbar",         required_argument, 0,            ARG_GAP_BAR},
	{(char*)"gopen",        required_argument, 0,            'O'},
	{(char*)"gextend",      required_argument, 0,            'E'},
	{(char*)"qseq",         no_argument,       0,            ARG_QSEQ},
	{(char*)"policy",       required_argument, 0,            ARG_ALIGN_POLICY},
	{(char*)"preset",       required_argument, 0,            'P'},
	{(char*)"seed-summ",    no_argument,       0,            ARG_SEED_SUMM},
	{(char*)"seed-summary", no_argument,       0,            ARG_SEED_SUMM},
	{(char*)"overhang",     no_argument,       0,            ARG_OVERHANG},
	{(char*)"no-cache",     no_argument,       0,            ARG_NO_CACHE},
	{(char*)"cache",        no_argument,       0,            ARG_USE_CACHE},
	{(char*)"454",          no_argument,       0,            ARG_NOISY_HPOLY},
	{(char*)"ion-torrent",  no_argument,       0,            ARG_NOISY_HPOLY},
	{(char*)"no-mixed",     no_argument,       0,            ARG_NO_MIXED},
	{(char*)"no-discordant",no_argument,       0,            ARG_NO_DISCORDANT},
	{(char*)"local",        no_argument,       0,            ARG_LOCAL},
	{(char*)"scan-narrowed",no_argument,       0,            ARG_SCAN_NARROWED},
	{(char*)"no-sse",       no_argument,       0,            ARG_NO_SSE},
	{(char*)"qc-filter",    no_argument,       0,            ARG_QC_FILTER},
	{(char*)"bwa-sw-like",  required_argument, 0,            ARG_BWA_SW_LIKE},
	{(char*)"multiseed",        required_argument, 0,        ARG_MULTISEED_IVAL},
	{(char*)"multiseed-const",  required_argument, 0,        ARG_MULTISEED_IVAL_CONST},
	{(char*)"multiseed-linear", required_argument, 0,        ARG_MULTISEED_IVAL_LINEAR},
	{(char*)"multiseed-sqrt",   required_argument, 0,        ARG_MULTISEED_IVAL_SQRT},
	{(char*)"multiseed-log",    required_argument, 0,        ARG_MULTISEED_IVAL_LOG},
	{(char*)"scores",           required_argument, 0,        ARG_SCORES},
	{(char*)"score-min",        required_argument, 0,        ARG_SCORE_MIN_LINEAR},
	{(char*)"score-min-const",  required_argument, 0,        ARG_SCORE_MIN_CONST},
	{(char*)"score-min-linear", required_argument, 0,        ARG_SCORE_MIN_LINEAR},
	{(char*)"score-min-sqrt",   required_argument, 0,        ARG_SCORE_MIN_SQRT},
	{(char*)"score-min-log",    required_argument, 0,        ARG_SCORE_MIN_LOG},
	{(char*)"n-ceil",           required_argument, 0,        ARG_N_CEIL},
	{(char*)"dpad",             required_argument, 0,        ARG_DPAD},
	{(char*)"mapq-top-coeff",   required_argument, 0,        ARG_MAPQ_TOP_COEFF},
	{(char*)"mapq-bot-coeff",   required_argument, 0,        ARG_MAPQ_BOT_COEFF},
	{(char*)"mapq-max",         required_argument, 0,        ARG_MAPQ_MAX},
	{(char*)"mapq-print-inputs",no_argument,       0,        ARG_SAM_PRINT_YI},
	{(char*)"very-fast",        no_argument,       0,        ARG_PRESET_VERY_FAST},
	{(char*)"fast",             no_argument,       0,        ARG_PRESET_FAST},
	{(char*)"sensitive",        no_argument,       0,        ARG_PRESET_SENSITIVE},
	{(char*)"very-sensitive",   no_argument,       0,        ARG_PRESET_VERY_SENSITIVE},
	{(char*)"no-score-priority",no_argument,       0,        ARG_NO_SCORE_PRIORITY},
	{(char*)0, 0, 0, 0} // terminator
};

/**
 * Print a summary usage message to the provided output stream.
 */
static void printUsage(ostream& out) {
	out << "Usage: " << endl
	    << "  bowtie2 [options]* <bt2-index> {-1 <m1> -2 <m2> | --12 <r> | <s>} [<hit>]" << endl
	    << endl
	    << "  <m1>    Comma-separated list of files containing upstream mates (or the" << endl
	    << "          sequences themselves, if -c is set) paired with mates in <m2>" << endl
	    << "  <m2>    Comma-separated list of files containing downstream mates (or the" << endl
	    << "          sequences themselves if -c is set) paired with mates in <m1>" << endl
	    << "  <r>     Comma-separated list of files containing Crossbow-style reads.  Can be" << endl
	    << "          a mixture of paired and unpaired.  Specify \"-\" for stdin." << endl
	    << "  <s>     Comma-separated list of files containing unpaired reads, or the" << endl
	    << "          sequences themselves, if -c is set.  Specify \"-\" for stdin." << endl
	    << "  <hit>   File to write hits to (default: stdout)" << endl
	    << "Input:" << endl
	    << "  -q                 query input files are FASTQ .fq/.fastq (default)" << endl
	    << "  -f                 query input files are (multi-)FASTA .fa/.mfa" << endl
	    << "  -r                 query input files are raw one-sequence-per-line" << endl
	    << "  --qseq             query input files are in Illumina's qseq format" << endl
	    << "  -c                 query sequences given on cmd line (as <mates>, <singles>)" << endl
	    //<< "  -C                 reads and index are in colorspace" << endl
	    //<< "  -Q/--quals <file>  QV file(s) corresponding to CSFASTA inputs; use with -f -C" << endl
	    //<< "  --Q1/--Q2 <file>   same as -Q, but for mate files 1 and 2 respectively" << endl
	    << "  -s/--skip <int>    skip the first <int> reads/pairs in the input" << endl
	    << "  -u/--qupto <int>   stop after first <int> reads/pairs (excl. skipped reads)" << endl
	    << "  -5/--trim5 <int>   trim <int> bases from 5' (left) end of reads" << endl
	    << "  -3/--trim3 <int>   trim <int> bases from 3' (right) end of reads" << endl
	    << "  --phred33-quals    input quals are Phred+33 (default)" << endl
	    << "  --phred64-quals    input quals are Phred+64 (same as --solexa1.3-quals)" << endl
	    << "  --solexa-quals     input quals are from GA Pipeline ver. < 1.3" << endl
	    << "  --solexa1.3-quals  input quals are from GA Pipeline ver. >= 1.3" << endl
	    << "  --integer-quals    qualities are given as space-separated integers (not ASCII)" << endl
	    << "Alignment:" << endl
		<< "  --scores           " << endl
		<< "  --score-min        " << endl
		<< "  --multiseed        " << endl
		<< "  --local            set alignment defaults to do BWA-SW-like local alignment" << endl
	    << "  --nomaqround       disable Maq-like quality rounding (nearest 10 <= 30)" << endl
	    << "  --nofw             do not align forward (original) version of read" << endl
	    << "  --norc             do not align reverse-complemented version of read" << endl
		<< "Paired-end:" << endl
	    << "  -I/--minins <int>  minimum fragment length (default: 0)" << endl
	    << "  -X/--maxins <int>  maximum fragment length (default: 450)" << endl
	    << "  --fr/--rf/--ff     -1, -2 mates align fw/rev, rev/fw, fw/fw (default: --fr)" << endl
		<< "  --no-mixed         report only paired alns, ignore unpaired" << endl
		<< "  --no-discordant    report only concordant paired-end alns, ignore discordant" << endl
	    << "Reporting:" << endl
	    << "  -M <int>           look for at least <int>+1 hits; report 1 with MAPQ (def.: 1)" << endl
		<< "   OR" << endl
	    << "  -k <int>           report up to <int> good alignments per read (default: 1)" << endl
		<< "   OR" << endl
	    << "  -a/--all           report all alignments per read (much slower than low -k)" << endl
	    << "Output:" << endl
	    << "  -t/--time          print wall-clock time taken by search phases" << endl
	    << "  --quiet            print nothing but the alignments" << endl
	    << "  --refidx           refer to ref. seqs by 0-based index rather than name" << endl
	    << "  --sam-nohead       supppress header lines (starting with @)" << endl
	    << "  --sam-nosq         supppress @SQ header lines" << endl
	    << "  --sam-RG <text>    add <text> (usually \"lab:value\") to @RG line of SAM header" << endl
	    << "Performance:" << endl
	    << "  -o/--offrate <int> override offrate of index; must be >= index's offrate" << endl
#ifdef BOWTIE_PTHREADS
	    << "  -p/--threads <int> number of alignment threads to launch (default: 1)" << endl
#endif
#ifdef BOWTIE_MM
	    << "  --mm               use memory-mapped I/O for index; many 'bowtie's can share" << endl
#endif
#ifdef BOWTIE_SHARED_MEM
	    << "  --shmem            use shared mem for index; many 'bowtie's can share" << endl
#endif
	    << "Other:" << endl
	    << "  --seed <int>       seed for random number generator" << endl
	    << "  --verbose          verbose output (for debugging)" << endl
	    << "  --version          print version information and quit" << endl
	    << "  -h/--help          print this usage message" << endl
	    ;
}

/**
 * Parse an int out of optarg and enforce that it be at least 'lower';
 * if it is less than 'lower', than output the given error message and
 * exit with an error and a usage message.
 */
static int parseInt(int lower, int upper, const char *errmsg, const char *arg) {
	long l;
	char *endPtr= NULL;
	l = strtol(arg, &endPtr, 10);
	if (endPtr != NULL) {
		if (l < lower || l > upper) {
			cerr << errmsg << endl;
			printUsage(cerr);
			throw 1;
		}
		return (int32_t)l;
	}
	cerr << errmsg << endl;
	printUsage(cerr);
	throw 1;
	return -1;
}

/**
 * Parse from optarg by default.
 */
static int parseInt(int lower, const char *errmsg) {
	return parseInt(lower, INT_MAX, errmsg, optarg);
}

/**
 * Upper is INT_MAX by default.
 */
static int parseInt(int lower, const char *errmsg, const char *arg) {
	return parseInt(lower, INT_MAX, errmsg, arg);
}

/**
 * Parse a T string 'str'.
 */
template<typename T>
T parse(const char *s) {
	T tmp;
	stringstream ss(s);
	ss >> tmp;
	return tmp;
}

/**
 * Parse a pair of Ts from a string, 'str', delimited with 'delim'.
 */
template<typename T>
pair<T, T> parsePair(const char *str, char delim) {
	string s(str);
	EList<string> ss;
	tokenize(s, delim, ss);
	pair<T, T> ret;
	ret.first = parse<T>(ss[0].c_str());
	ret.second = parse<T>(ss[1].c_str());
	return ret;
}

/**
 * Parse a pair of Ts from a string, 'str', delimited with 'delim'.
 */
template<typename T>
void parseTuple(const char *str, char delim, EList<T>& ret) {
	string s(str);
	EList<string> ss;
	tokenize(s, delim, ss);
	for(size_t i = 0; i < ss.size(); i++) {
		ret.push_back(parse<T>(ss[i].c_str()));
	}
}

static string applyPreset(const string& sorig, Presets& presets) {
	string s = sorig;
	size_t found = s.find("%LOCAL%");
	if(found != string::npos) {
		s.replace(found, strlen("%LOCAL%"), localAlign ? "-local" : "");
	}
	if(gVerbose) {
		cerr << "Applying preset: '" << s << "' using preset menu '"
			 << presets.name() << "'" << endl;
	}
	string pol, tmp;
	presets.apply(s, pol, tmp);
	assert(tmp.empty());
	return pol;
}

/**
 * Read command-line arguments
 */
static void parseOptions(int argc, const char **argv) {
	int option_index = 0;
	int next_option;
	bool saw_M = false;
	bool saw_a = false;
	bool saw_k = false;
	EList<string> presetList; presetList.clear();
	if(startVerbose) { cerr << "Parsing options: "; logTime(cerr, true); }
	do {
		next_option = getopt_long(
			argc, const_cast<char**>(argv),
			short_options, long_options, &option_index);
		switch (next_option) {
			case '1': tokenize(optarg, ",", mates1); break;
			case '2': tokenize(optarg, ",", mates2); break;
			case ARG_ONETWO: tokenize(optarg, ",", mates12); format = TAB_MATE; break;
			case 'f': format = FASTA; break;
			case 'F': {
				format = FASTA_CONT;
				pair<uint32_t, uint32_t> p = parsePair<uint32_t>(optarg, ',');
				fastaContLen = p.first;
				fastaContFreq = p.second;
				break;
			}
			case ARG_BWA_SW_LIKE: {
				pair<float, float> p = parsePair<float>(optarg, ',');
				bwaSwLikeC = p.first;
				bwaSwLikeT = p.second;
				bwaSwLike = true;
				// -a INT   Score of a match [1]
				// -b INT   Mismatch penalty [3]
				// -q INT   Gap open penalty [5]
				// -r INT   Gap extension penalty. The penalty for a contiguous
				//          gap of size k is q+k*r. [2] 
				polstr += ";MA=1;MMP=C3;RDG=5,2;RFG=5,2";
				break;
			}
			case 'q': format = FASTQ; break;
			case 'r': format = RAW; break;
			case 'c': format = CMDLINE; break;
			case ARG_QSEQ: format = QSEQ; break;
			case 'C': gColor = true; break;
			case 'I':
				gMinInsert = parseInt(0, "-I arg must be positive");
				break;
			case 'X':
				gMaxInsert = parseInt(1, "-X arg must be at least 1");
				break;
			case ARG_NO_DISCORDANT:
				gReportDiscordant = false;
				break;
			case ARG_NO_MIXED:
				gReportMixed = false;
				break;
			case 's':
				skipReads = (uint32_t)parseInt(0, "-s arg must be positive");
				break;
			case ARG_FF: gMate1fw = true;  gMate2fw = true;  mateFwSet = true; break;
			case ARG_RF: gMate1fw = false; gMate2fw = true;  mateFwSet = true; break;
			case ARG_FR: gMate1fw = true;  gMate2fw = false; mateFwSet = true; break;
			case ARG_USE_SPINLOCK: useSpinlock = false; break;
			case ARG_SHMEM: useShmem = true; break;
			case ARG_COLOR_SEQ: gColorSeq = true; break;
			case ARG_COLOR_EDIT: gColorEdit = true; break;
			case ARG_COLOR_QUAL: gColorQual = true; break;
			case ARG_SEED_SUMM: seedSummaryOnly = true; break;
			case ARG_SUPPRESS_FIELDS: {
				EList<string> supp;
				tokenize(optarg, ",", supp);
				for(size_t i = 0; i < supp.size(); i++) {
					int ii = parseInt(1, "--suppress arg must be at least 1", supp[i].c_str());
					suppressOuts[ii-1] = true;
				}
				break;
			}
			case ARG_MM: {
#ifdef BOWTIE_MM
				useMm = true;
				break;
#else
				cerr << "Memory-mapped I/O mode is disabled because bowtie was not compiled with" << endl
				     << "BOWTIE_MM defined.  Memory-mapped I/O is not supported under Windows.  If you" << endl
				     << "would like to use memory-mapped I/O on a platform that supports it, please" << endl
				     << "refrain from specifying BOWTIE_MM=0 when compiling Bowtie." << endl;
				throw 1;
#endif
			}
			case ARG_MMSWEEP: mmSweep = true; break;
			case ARG_HADOOPOUT: hadoopOut = true; break;
			case ARG_SOLEXA_QUALS: solexaQuals = true; break;
			case ARG_INTEGER_QUALS: integerQuals = true; break;
			case ARG_PHRED64: phred64Quals = true; break;
			case ARG_PHRED33: solexaQuals = false; phred64Quals = false; break;
			case ARG_NOMAQROUND: gNoMaqRound = true; break;
			case ARG_COLOR_KEEP_ENDS: gColorExEnds = false; break;
			case ARG_OVERHANG: gReportOverhangs = true; break;
			case ARG_NO_CACHE: msNoCache = true; break;
			case ARG_USE_CACHE: msNoCache = false; break;
			case ARG_SNPPHRED: gSnpPhred = parseInt(0, "--snpphred must be at least 0"); break;
			case ARG_SNPFRAC: {
				double p = parse<double>(optarg);
				if(p <= 0.0) {
					cerr << "Error: --snpfrac parameter must be > 0.0" << endl;
					throw 1;
				}
				p = (log10(p) * -10);
				gSnpPhred = (int)(p + 0.5);
				if(gSnpPhred < 10)
				cout << "gSnpPhred: " << gSnpPhred << endl;
				break;
			}
			case 'z': {
				cerr << "Error: -z/--phased mode is no longer supported" << endl;
				throw 1;
			}
			case ARG_REFIDX: noRefNames = true; break;
			case ARG_FUZZY: fuzzy = true; break;
			case ARG_FULLREF: fullRef = true; break;
			case ARG_GAP_BAR:
				gGapBarrier = parseInt(1, "--gbar must be no less than 1");
				break;
			case ARG_SEED:
				seed = parseInt(0, "--seed arg must be at least 0");
				break;
			case 'u':
				qUpto = (uint32_t)parseInt(1, "-u/--qupto arg must be at least 1");
				break;
			case 'Q':
				tokenize(optarg, ",", qualities);
				integerQuals = true;
				break;
			case ARG_QUALS1:
				tokenize(optarg, ",", qualities1);
				integerQuals = true;
				break;
			case ARG_QUALS2:
				tokenize(optarg, ",", qualities2);
				integerQuals = true;
				break;
			case ARG_CACHE_LIM:
				cacheLimit = (uint32_t)parseInt(1, "--cachelim arg must be at least 1");
				break;
			case ARG_CACHE_SZ:
				cacheSize = (uint32_t)parseInt(1, "--cachesz arg must be at least 1");
				cacheSize *= (1024 * 1024); // convert from MB to B
				break;
			case 'p':
#ifndef BOWTIE_PTHREADS
				cerr << "-p/--threads is disabled because bowtie was not compiled with pthreads support" << endl;
				throw 1;
#endif
				nthreads = parseInt(1, "-p/--threads arg must be at least 1");
				break;
			case ARG_FILEPAR:
#ifndef BOWTIE_PTHREADS
				cerr << "--filepar is disabled because bowtie was not compiled with pthreads support" << endl;
				throw 1;
#endif
				fileParallel = true;
				break;
			case '3': gTrim3 = parseInt(0, "-3/--trim3 arg must be at least 0"); break;
			case '5': gTrim5 = parseInt(0, "-5/--trim5 arg must be at least 0"); break;
			case 'h': printUsage(cout); throw 0; break;
			case ARG_USAGE: printUsage(cout); throw 0; break;
			//
			// NOTE that unlike in Bowtie 1, -M, -a and -k are mutually
			// exclusive here.
			//
			case 'M': {
				msample = true;
				mhits = (uint32_t)parseInt(1, "-M arg must be at least 1");
				if(saw_a || saw_k) {
					cerr << "Warning: -M, -k and -a are mutually exclusive. "
					     << "-M will override" << endl;
				}
				saw_M = true;
				break;
			}
			case 'a': {
				msample = false;
				allHits = true;
				mhits = 0; // disable -M
				if(saw_M || saw_k) {
					cerr << "Warning: -M, -k and -a are mutually exclusive. "
					     << "-a will override" << endl;
				}
				saw_a = true;
				break;
			}
			case 'k': {
				msample = false;
				khits = (uint32_t)parseInt(1, "-k arg must be at least 1");
				mhits = 0; // disable -M
				if(saw_M || saw_a) {
					cerr << "Warning: -M, -k and -a are mutually exclusive. "
					     << "-k will override" << endl;
				}
				saw_k = true;
				break;
			}
			case ARG_VERBOSE: gVerbose = 1; break;
			case ARG_STARTVERBOSE: startVerbose = true; break;
			case ARG_QUIET: gQuiet = true; break;
			case ARG_SANITY: sanityCheck = true; break;
			case 't': timing = true; break;
			case ARG_METRIC_IVAL: {
#ifdef BOWTIE_PTHREADS
				metricsIval = parseInt(1, "--metrics arg must be at least 1");
#else
				cerr << "Must compile with BOWTIE_PTHREADS to use --metrics" << endl;
				throw 1;
#endif
				break;
			}
			case ARG_METRIC_FILE: metricsFile = optarg; break;
			case ARG_METRIC_STDERR: metricsStderr = true; break;
			case ARG_NO_FW: gNofw = true; break;
			case ARG_NO_RC: gNorc = true; break;
			case ARG_SAM_NO_QNAME_TRUNC: samTruncQname = false; break;
			case ARG_SAM_OMIT_SEC_SEQ: samOmitSecSeqQual = true; break;
			case ARG_SAM_NOHEAD: samNoHead = true; break;
			case ARG_SAM_NOSQ: samNoSQ = true; break;
			case ARG_SAM_PRINT_YI: sam_print_yi = true; break;
			case ARG_SAM_RG: {
				string arg = optarg;
				if(arg.substr(0, 3) == "ID:") {
					rgid = "\t";
					rgid += arg;
					rgs_optflag = "RG:Z:" + arg.substr(3);
				} else {
					rgs += '\t';
					rgs += arg;
				}
				break;
			}
			case ARG_PRINT_PLACEHOLDERS: printPlaceholders = true; break;
			case ARG_PRINT_FLAGS: printFlags = true; break;
			case ARG_PRINT_PARAMS: printParams = true; break;
			case ARG_DUMP_PATS: patDumpfile = optarg; break;
			case ARG_PARTITION: partitionSz = parse<int>(optarg); break;
			case ARG_DPAD:
				maxhalf = parseInt(0, "--dpad must be no less than 0");
				break;
			case ARG_ORIG:
				if(optarg == NULL || strlen(optarg) == 0) {
					cerr << "--orig arg must be followed by a string" << endl;
					printUsage(cerr);
					throw 1;
				}
				origString = optarg;
				break;
			case ARG_LOCAL: localAlign = true; break;
			case ARG_SCAN_NARROWED: scanNarrowed = true; break;
			case ARG_NO_SSE: noSse = true; break;
			case ARG_QC_FILTER: qcFilter = true; break;
			case ARG_NO_SCORE_PRIORITY: sortByScore = false; break;
			case ARG_NOISY_HPOLY: noisyHpolymer = true; break;
			case ARG_PRESET_VERY_FAST: {
				presetList.push_back("very-fast%LOCAL%"); break;
			}
			case ARG_PRESET_FAST: {
				presetList.push_back("fast%LOCAL%"); break;
			}
			case ARG_PRESET_SENSITIVE: {
				presetList.push_back("sensitive%LOCAL%"); break;
			}
			case ARG_PRESET_VERY_SENSITIVE: {
				presetList.push_back("very-sensitive%LOCAL%"); break;
			}
			case 'P': {
				presetList.push_back(optarg);
				break;
			}
			case ARG_ALIGN_POLICY: {
				polstr += ";";
				polstr += optarg;
				break;
			}
			case ARG_MULTISEED_IVAL: {
				polstr += ";";
				// Split argument by comma
				EList<string> args;
				tokenize(optarg, ",", args);
				if(args.size() > 11 || args.size() == 0) {
					cerr << "Error: expected 11 or fewer comma-separated "
					     << "arguments to --multiseed option, got "
						 << args.size() << endl;
					throw 1;
				}
				// Seed mm and length arguments
				polstr += "SEED=";
				polstr += (args[0]);
				if(args.size() > 1) {
					polstr += ("," + args[1]);
				}
				// Interval-settings arguments
				if(args.size() > 2) {
					// Function type
					polstr += (";IVAL=" + args[2]);
				}
				if(args.size() > 3) {
					// Constant term
					polstr += ("," + args[3]);
				}
				if(args.size() > 4) {
					// Coefficient
					polstr += ("," + args[4]);
				}
				// Arguments for # seed positions to examine
				if(args.size() > 5) {
					// Function type
					polstr += (";POSF=" + args[5]);
				}
				if(args.size() > 6) {
					// Constant term
					polstr += ("," + args[6]);
				}
				if(args.size() > 7) {
					// Coefficient
					polstr += ("," + args[7]);
				}
				// Arguments for # rows to examine per range
				if(args.size() > 8) {
					// Function type
					polstr += (";ROWM=" + args[8]);
				}
				if(args.size() > 9) {
					// Constant term
					polstr += ("," + args[9]);
				}
				if(args.size() > 10) {
					// Coefficient
					polstr += ("," + args[10]);
				}
				break;
			}
			case ARG_MULTISEED_IVAL_CONST:
			case ARG_MULTISEED_IVAL_LINEAR:
			case ARG_MULTISEED_IVAL_SQRT:
			case ARG_MULTISEED_IVAL_LOG: {
				polstr += ";";
				const char *type =
					(ARG_MULTISEED_IVAL_CONST  ? "C" :
					(ARG_MULTISEED_IVAL_LINEAR ? "L" :
					(ARG_MULTISEED_IVAL_SQRT   ? "S" :
					(ARG_MULTISEED_IVAL_LOG    ? "G" : "?"))));
				if(type[0] == '?') { throw 1; }
				// Split argument by comma
				EList<string> args;
				tokenize(optarg, ",", args);
				if(args.size() > 7 || args.size() == 0) {
					cerr << "Error: expected 7 or fewer comma-separated "
					     << "arguments to --multiseed-* option, got "
						 << args.size() << endl;
					throw 1;
				}
				// Seed mm and length arguments
				polstr += "SEED=";
				polstr += (args[0]);
				if(args.size() > 1) {
					polstr += ("," + args[1]);
				}
				// Interval-settings arguments
				if(args.size() > 2) {
					polstr += ";IVAL=";
					polstr += type;
					polstr += ("," + args[2]);
				}
				if(args.size() > 3) {
					polstr += ("," + args[3]);
				}
				// Arguments for # seeds to examine
				if(args.size() > 4) {
					polstr += (";POSF=" + args[4]);
				}
				if(args.size() > 5) {
					polstr += ("," + args[5]);
				}
				if(args.size() > 6) {
					polstr += (";ROWM=" + args[6]);
				}
				break;
			}
			case ARG_N_CEIL: {
				polstr += ";";
				// Split argument by comma
				EList<string> args;
				tokenize(optarg, ",", args);
				if(args.size() > 2 || args.size() == 0) {
					cerr << "Error: expected 2 or fewer comma-separated "
					     << "arguments to --n-ceil option, got "
						 << args.size() << endl;
					throw 1;
				}
				polstr += ("NCEIL=" + args[0]);
				if(args.size() > 1) {
					polstr += ("," + (args[1]));
				}
				break;
			}
			case ARG_SCORES: {
				polstr += ";";
				// MA=xx (default: MA=0, or MA=2 if --local is set)
				// MMP={Cxx|Q|RQ} (default: MMP=C6)
				// NP={Cxx|Q|RQ} (default: NP=C1)
				// RDG=xx,yy (default: RDG=5,3)
				// RFG=xx,yy (default: RFG=5,3)
				// Split argument by comma
				EList<string> args;
				tokenize(optarg, ",", args);
				if(args.size() > 7 || args.size() == 0) {
					cerr << "Error: expected 7 or fewer comma-separated "
					     << "arguments to --scores option, got "
						 << args.size() << endl;
					throw 1;
				}
				polstr += ("MA=" + args[0]);
				if(args.size() > 1) {
					polstr += (";MMP=" + args[1]);
				}
				if(args.size() > 2) {
					polstr += (";NP=" + args[2]);
				}
				if(args.size() > 3) {
					polstr += (";RDG=" + args[3]);
				}
				if(args.size() > 4) {
					polstr += ("," + args[4]);
				}
				if(args.size() > 5) {
					polstr += (";RFG=" + args[5]);
				}
				if(args.size() > 6) {
					polstr += ("," + args[6]);
				}
				break;
			}
			case ARG_SCORE_MIN_CONST:
			case ARG_SCORE_MIN_LINEAR:
			case ARG_SCORE_MIN_SQRT:
			case ARG_SCORE_MIN_LOG: {
				polstr += ";";
				EList<string> args;
				tokenize(optarg, ",", args);
				if(args.size() > 3 && args.size() == 0) {
					cerr << "Error: expected 3 or fewer comma-separated "
					     << "arguments to --n-ceil option, got "
						 << args.size() << endl;
					throw 1;
				}
				polstr += ("MIN=" + args[0]);
				if(args.size() > 1) {
					polstr += ("," + args[1]);
				}
				if(args.size() > 2) {
					polstr += ("," + args[2]);
				}
				break;
			}
			case ARG_MAPQ_TOP_COEFF:
				mapqTopCoeff = parse<double>(optarg);
				break;
			case ARG_MAPQ_BOT_COEFF:
				mapqBotCoeff = parse<double>(optarg);
				break;
			case ARG_MAPQ_MAX:
				mapqMax = parse<double>(optarg);
				break;
			case -1: break; /* Done with options. */
			case 0:
				if (long_options[option_index].flag != 0)
					break;
			default:
				printUsage(cerr);
				throw 1;
		}
	} while(next_option != -1);
	// Now parse all the presets.  Might want to pick which presets version to
	// use according to other parameters.
	auto_ptr<Presets> presets(new PresetsV0());
	// Apply default preset
	if(!defaultPreset.empty()) {
		polstr = applyPreset(defaultPreset, *presets.get()) + polstr;
	}
	// Apply specified presets
	for(size_t i = 0; i < presetList.size(); i++) {
		polstr += applyPreset(presetList[i], *presets.get());
	}
	// Remove initial semicolons
	while(!polstr.empty() && polstr[0] == ';') {
		polstr = polstr.substr(1);
	}
	if(gVerbose) {
		cerr << "Final policy string: '" << polstr << "'" << endl;
	}
	SeedAlignmentPolicy::parseString(
		polstr,
		localAlign,
		noisyHpolymer,
		bonusMatchType,
		bonusMatch,
		penMmcType,
		penMmc,
		penSnp,
		penNType,
		penN,
		penRdGapConst,
		penRfGapConst,
		penRdGapLinear,
		penRfGapLinear,
		scoreMin,
		scoreFloor,
		nCeil,
		penNCatPair,
		multiseedMms,
		multiseedLen,
		msIval,
		posfrac,
		rowmult);
	if(localAlign) {
		gRowLow = 0;
	} else {
		gRowLow = -1;
	}
	if(mates1.size() != mates2.size()) {
		cerr << "Error: " << mates1.size() << " mate files/sequences were specified with -1, but " << mates2.size() << endl
		     << "mate files/sequences were specified with -2.  The same number of mate files/" << endl
		     << "sequences must be specified with -1 and -2." << endl;
		throw 1;
	}
	if(qualities.size() && format != FASTA) {
		cerr << "Error: one or more quality files were specified with -Q but -f was not" << endl
		     << "enabled.  -Q works only in combination with -f and -C." << endl;
		throw 1;
	}
	if(qualities.size() && !gColor) {
		cerr << "Error: one or more quality files were specified with -Q but -C was not" << endl
		     << "enabled.  -Q works only in combination with -f and -C." << endl;
		throw 1;
	}
	if(qualities1.size() && format != FASTA) {
		cerr << "Error: one or more quality files were specified with --Q1 but -f was not" << endl
		     << "enabled.  --Q1 works only in combination with -f and -C." << endl;
		throw 1;
	}
	if(qualities1.size() && !gColor) {
		cerr << "Error: one or more quality files were specified with --Q1 but -C was not" << endl
		     << "enabled.  --Q1 works only in combination with -f and -C." << endl;
		throw 1;
	}
	if(qualities2.size() && format != FASTA) {
		cerr << "Error: one or more quality files were specified with --Q2 but -f was not" << endl
		     << "enabled.  --Q2 works only in combination with -f and -C." << endl;
		throw 1;
	}
	if(qualities2.size() && !gColor) {
		cerr << "Error: one or more quality files were specified with --Q2 but -C was not" << endl
		     << "enabled.  --Q2 works only in combination with -f and -C." << endl;
		throw 1;
	}
	if(qualities1.size() > 0 && mates1.size() != qualities1.size()) {
		cerr << "Error: " << mates1.size() << " mate files/sequences were specified with -1, but " << qualities1.size() << endl
		     << "quality files were specified with --Q1.  The same number of mate and quality" << endl
		     << "files must sequences must be specified with -1 and --Q1." << endl;
		throw 1;
	}
	if(qualities2.size() > 0 && mates2.size() != qualities2.size()) {
		cerr << "Error: " << mates2.size() << " mate files/sequences were specified with -2, but " << qualities2.size() << endl
		     << "quality files were specified with --Q2.  The same number of mate and quality" << endl
		     << "files must sequences must be specified with -2 and --Q2." << endl;
		throw 1;
	}
	// Check for duplicate mate input files
	if(format != CMDLINE) {
		for(size_t i = 0; i < mates1.size(); i++) {
			for(size_t j = 0; j < mates2.size(); j++) {
				if(mates1[i] == mates2[j] && !gQuiet) {
					cerr << "Warning: Same mate file \"" << mates1[i] << "\" appears as argument to both -1 and -2" << endl;
				}
			}
		}
	}
	// If both -s and -u are used, we need to adjust qUpto accordingly
	// since it uses patid to know if we've reached the -u limit (and
	// patids are all shifted up by skipReads characters)
	if(qUpto + skipReads > qUpto) {
		qUpto += skipReads;
	}
	if(useShmem && useMm && !gQuiet) {
		cerr << "Warning: --shmem overrides --mm..." << endl;
		useMm = false;
	}
	if(gSnpPhred <= 10 && gColor && !gQuiet) {
		cerr << "Warning: the colorspace SNP penalty (--snpphred) is very low: " << gSnpPhred << endl;
	}
	if(!mateFwSet) {
		if(gColor) {
			// Set colorspace default (--ff)
			gMate1fw = true;
			gMate2fw = true;
		} else {
			// Set nucleotide space default (--fr)
			gMate1fw = true;
			gMate2fw = false;
		}
	}
	size_t fieldsSuppressed = 0;
	for(size_t i = 0; i < suppressOuts.size(); i++) {
		if(suppressOuts[i]) fieldsSuppressed++;
	}
	if(outType != OUTPUT_FULL && fieldsSuppressed > 0 && !gQuiet) {
		cerr << "Warning: Ignoring --suppress because output type is not default." << endl;
		cerr << "         --suppress is only available for the default output type." << endl;
		suppressOuts.fill(false);
	}
	if(gGapBarrier < 1) {
		cerr << "Warning: --gbar was set less than 1 (=" << gGapBarrier
		     << "); setting to 1 instead" << endl;
		gGapBarrier = 1;
	}
	if(gColor && gColorExEnds) {
		gGapBarrier++;
	}
	if(multiseedMms >= multiseedLen) {
		assert_gt(multiseedLen, 0);
		cerr << "Warning: seed mismatches (" << multiseedMms
		     << ") is less than seed length (" << multiseedLen
			 << "); setting mismatches to " << (multiseedMms-1)
			 << " instead" << endl;
		multiseedMms = multiseedLen-1;
	}
}

static const char *argv0 = NULL;

/// Create a PatternSourcePerThread for the current thread according
/// to the global params and return a pointer to it
static PatternSourcePerThreadFactory*
createPatsrcFactory(PairedPatternSource& _patsrc, int tid) {
	PatternSourcePerThreadFactory *patsrcFact;
	patsrcFact = new WrappedPatternSourcePerThreadFactory(_patsrc);
	assert(patsrcFact != NULL);
	return patsrcFact;
}

#ifdef BOWTIE_PTHREADS
#define PTHREAD_ATTRS (PTHREAD_CREATE_JOINABLE | PTHREAD_CREATE_DETACHED)
#endif

static PairedPatternSource*     multiseed_patsrc;
static Ebwt*                    multiseed_ebwtFw;
static Ebwt*                    multiseed_ebwtBw;
static Scoring*                 multiseed_sc;
static EList<Seed>*             multiseed_seeds;
static BitPairReference*        multiseed_refs;
static AlignmentCache*          multiseed_ca; // seed cache
static AlnSink*                 multiseed_msink;
static OutFileBuf*              multiseed_metricsOfb;

static EList<ReadCounterSink*>* multiseed_readCounterSink;
static EList<SeedHitSink*>*     multiseed_seedHitSink;
static EList<SeedCounterSink*>* multiseed_seedCounterSink;
static EList<SeedActionSink*>*  multiseed_seedActionSink;
static EList<SwCounterSink*>*   multiseed_swCounterSink;
static EList<SwActionSink*>*    multiseed_swActionSink;

/**
 * Metrics for measuring the work done by the outer read alignment
 * loop.
 */
struct OuterLoopMetrics {

	OuterLoopMetrics() { reset(); MUTEX_INIT(lock); }

	/**
	 * Set all counters to 0.
	 */
	void reset() {
		reads = bases = srreads = srbases =
		freads = fbases = ureads = ubases = 0;
	}

	/**
	 * Sum the counters in m in with the conters in this object.  This
	 * is the only safe way to update an OuterLoopMetrics that's shared
	 * by multiple threads.
	 */
	void merge(
		const OuterLoopMetrics& m,
		bool getLock = false)
	{
		ThreadSafe ts(&lock, getLock);
		reads += m.reads;
		bases += m.bases;
		srreads += m.srreads;
		srbases += m.srbases;
		freads += m.freads;
		fbases += m.fbases;
		ureads += m.ureads;
		ubases += m.ubases;
	}

	uint64_t reads;   // total reads
	uint64_t bases;   // total bases
	uint64_t srreads; // same-read reads
	uint64_t srbases; // same-read bases
	uint64_t freads;  // filtered reads
	uint64_t fbases;  // filtered bases
	uint64_t ureads;  // unfiltered reads
	uint64_t ubases;  // unfiltered bases
	MUTEX_T lock;
};

/**
 * Collection of all relevant performance metrics when aligning in
 * multiseed mode.
 */
struct PerfMetrics {

	PerfMetrics() : first(true) { reset(); }

	/**
	 * Set all counters to 0.
	 */
	void reset() {
		olm.reset();
		sdm.reset();
		wlm.reset();
		swmSeed.reset();
		swmMate.reset();
		rpm.reset();
		dpSse8Seed.reset();   // 8-bit SSE seed extensions
		dpSse8Mate.reset();     // 8-bit SSE mate finds
		dpSse16Seed.reset();  // 16-bit SSE seed extensions
		dpSse16Mate.reset();    // 16-bit SSE mate finds
		
		olmu.reset();
		sdmu.reset();
		wlmu.reset();
		swmuSeed.reset();
		swmuMate.reset();
		rpmu.reset();
		dpSse8uSeed.reset();  // 8-bit SSE seed extensions
		dpSse8uMate.reset();    // 8-bit SSE mate finds
		dpSse16uSeed.reset(); // 16-bit SSE seed extensions
		dpSse16uMate.reset();   // 16-bit SSE mate finds
	}

	/**
	 * Merge a set of specific metrics into this object.
	 */
	void merge(
		const OuterLoopMetrics *ol,
		const SeedSearchMetrics *sd,
		const WalkMetrics *wl,
		const SwMetrics *swSeed,
		const SwMetrics *swMate,
		const ReportingMetrics *rm,
		const SSEMetrics *dpSse8Ex,
		const SSEMetrics *dpSse8Ma,
		const SSEMetrics *dpSse16Ex,
		const SSEMetrics *dpSse16Ma,
		bool getLock)
	{
		ThreadSafe ts(&lock, getLock);
		if(ol != NULL) {
			olmu.merge(*ol, false);
		}
		if(sd != NULL) {
			sdmu.merge(*sd, false);
		}
		if(wl != NULL) {
			wlmu.merge(*wl, false);
		}
		if(swSeed != NULL) {
			swmuSeed.merge(*swSeed, false);
		}
		if(swMate != NULL) {
			swmuMate.merge(*swMate, false);
		}
		if(rm != NULL) {
			rpmu.merge(*rm, false);
		}
		if(dpSse8Ex != NULL) {
			dpSse8uSeed.merge(*dpSse8Ex, false);
		}
		if(dpSse8Ma != NULL) {
			dpSse8uMate.merge(*dpSse8Ma, false);
		}
		if(dpSse16Ex != NULL) {
			dpSse16uSeed.merge(*dpSse16Ex, false);
		}
		if(dpSse16Ma != NULL) {
			dpSse16uMate.merge(*dpSse16Ma, false);
		}
	}

	/**
	 * Reports a matrix of results, incl. column labels, to an
	 * OutFileBuf.  Optionally also send results to stderr (unbuffered).
	 */
	void reportInterval(
		OutFileBuf* o,      // file to send output to
		bool metricsStderr, // additionally output to stderr?
		bool sync = true)
	{
		ThreadSafe ts(&lock, sync);
		time_t curtime = time(0);
		char buf[1024];
		if(first) {
			// 8. Aligned concordant pairs
			// 9. Aligned discordant pairs
			// 10. Pairs aligned non-uniquely
			// 11. Pairs that failed to align as a pair
			// 12. Aligned unpaired reads
			// 13. Unpaired reads aligned non-uniquely
			// 14. Unpaired reads that fail to align

			const char *str =
				/*  1 */ "Time"           "\t"
				/*  2 */ "Read"           "\t"
				/*  3 */ "Base"           "\t"
				/*  4 */ "SameRead"       "\t"
				/*  5 */ "SameReadBase"   "\t"
				/*  6 */ "UnfilteredRead" "\t"
				/*  7 */ "UnfilteredBase" "\t"
				
				/*  8 */ "Paired"         "\t"
				/*  9 */ "Unpaired"       "\t"
				/* 10 */ "AlConUni"       "\t"
				/* 11 */ "AlConRep"       "\t"
				/* 12 */ "AlConFail"      "\t"
				/* 13 */ "AlDis"          "\t"
				/* 14 */ "AlConFailUni"   "\t"
				/* 15 */ "AlConFailRep"   "\t"
				/* 16 */ "AlConFailFail"  "\t"
				/* 17 */ "AlConRepUni"    "\t"
				/* 18 */ "AlConRepRep"    "\t"
				/* 19 */ "AlConRepFail"   "\t"
				/* 20 */ "AlUnpUni"       "\t"
				/* 21 */ "AlUnpRep"       "\t"
				/* 22 */ "AlUnpFail"      "\t"
				
				/* 23 */ "SeedSearch"     "\t"
				/* 24 */ "IntraSCacheHit" "\t"
				/* 25 */ "InterSCacheHit" "\t"
				/* 26 */ "OutOfMemory"    "\t"
				/* 27 */ "AlBWOp"         "\t"
				/* 28 */ "AlBWBranch"     "\t"
				/* 29 */ "ResBWOp"        "\t"
				/* 30 */ "ResBWBranch"    "\t"
				/* 31 */ "ResResolve"     "\t"
				/* 32 */ "RefScanHit"     "\t"
				/* 33 */ "RefScanResolve" "\t"
				/* 34 */ "ResReport"      "\t"
				/* 35 */ "RedundantSHit"  "\t"
				
				/* 36 */ "DP16ExDps"      "\t"
				/* 37 */ "DP16ExDpSat"    "\t"
				/* 38 */ "DP16ExDpFail"   "\t"
				/* 39 */ "DP16ExDpSucc"   "\t"
				/* 40 */ "DP16ExCol"      "\t"
				/* 41 */ "DP16ExCell"     "\t"
				/* 42 */ "DP16ExInner"    "\t"
				/* 43 */ "DP16ExFixup"    "\t"
				/* 44 */ "DP16ExGathCell" "\t"
				/* 45 */ "DP16ExGathSol"  "\t"
				/* 46 */ "DP16ExBt"       "\t"
				/* 47 */ "DP16ExBtFail"   "\t"
				/* 48 */ "DP16ExBtSucc"   "\t"
				/* 49 */ "DP16ExBtCell"   "\t"

				/* 50 */ "DP8ExDps"       "\t"
				/* 51 */ "DP8ExDpSat"     "\t"
				/* 52 */ "DP8ExDpFail"    "\t"
				/* 53 */ "DP8ExDpSucc"    "\t"
				/* 54 */ "DP8ExCol"       "\t"
				/* 55 */ "DP8ExCell"      "\t"
				/* 56 */ "DP8ExInner"     "\t"
				/* 57 */ "DP8ExFixup"     "\t"
				/* 58 */ "DP8ExGathCell"  "\t"
				/* 59 */ "DP8ExGathSol"   "\t"
				/* 60 */ "DP8ExBt"        "\t"
				/* 61 */ "DP8ExBtFail"    "\t"
				/* 62 */ "DP8ExBtSucc"    "\t"
				/* 63 */ "DP8ExBtCell"    "\t"

				/* 64 */ "DP16MateDps"     "\t"
				/* 65 */ "DP16MateDpSat"   "\t"
				/* 66 */ "DP16MateDpFail"  "\t"
				/* 67 */ "DP16MateDpSucc"  "\t"
				/* 68 */ "DP16MateCol"     "\t"
				/* 69 */ "DP16MateCell"    "\t"
				/* 70 */ "DP16MateInner"   "\t"
				/* 71 */ "DP16MateFixup"   "\t"
				/* 72 */ "DP16MateGathCell""\t"
				/* 73 */ "DP16MateGathSol" "\t"
				/* 74 */ "DP16MateBt"      "\t"
				/* 75 */ "DP16MateBtFail"  "\t"
				/* 76 */ "DP16MateBtSucc"  "\t"
				/* 77 */ "DP16MateBtCell"  "\t"

				/* 78 */ "DP8MateDps"     "\t"
				/* 79 */ "DP8MateDpSat"   "\t"
				/* 80 */ "DP8MateDpFail"  "\t"
				/* 81 */ "DP8MateDpSucc"  "\t"
				/* 82 */ "DP8MateCol"     "\t"
				/* 83 */ "DP8MateCell"    "\t"
				/* 84 */ "DP8MateInner"   "\t"
				/* 85 */ "DP8MateFixup"   "\t"
				/* 86 */ "DP8MateGathCell""\t"
				/* 87 */ "DP8MateGathSol" "\t"
				/* 88 */ "DP8MateBt"      "\t"
				/* 89 */ "DP8MateBtFail"  "\t"
				/* 90 */ "DP8MateBtSucc"  "\t"
				/* 91 */ "DP8MateBtCell"  "\t"

				/* 92 */ "MemPeak"        "\t"
				/* 93 */ "UncatMemPeak"   "\t" // 0
				/* 94 */ "EbwtMemPeak"    "\t" // EBWT_CAT
				/* 95 */ "CacheMemPeak"   "\t" // CA_CAT
				/* 96 */ "ResolveMemPeak" "\t" // GW_CAT
				/* 97 */ "AlignMemPeak"   "\t" // AL_CAT
				/* 98 */ "DPMemPeak"      "\t" // DP_CAT
				/* 99 */ "MiscMemPeak"    "\t" // MISC_CAT
				/* 100 */ "DebugMemPeak"   "\t" // DEBUG_CAT
				
				"\n";
			
			if(o != NULL) o->writeChars(str);
			if(metricsStderr) cerr << str;
			first = false;
		}
		
		// 1. Current time in secs
		itoa10<time_t>(curtime, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 2. Reads
		itoa10<uint64_t>(olmu.reads, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 3. Bases
		itoa10<uint64_t>(olmu.bases, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 4. Same-read reads
		itoa10<uint64_t>(olmu.srreads, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 5. Same-read bases
		itoa10<uint64_t>(olmu.srbases, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 6. Unfiltered reads
		itoa10<uint64_t>(olmu.ureads, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 7. Unfiltered bases
		itoa10<uint64_t>(olmu.ubases, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }

		// 8. Paired reads
		itoa10<uint64_t>(rpmu.npaired, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 9. Unpaired reads
		itoa10<uint64_t>(rpmu.nunpaired, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 10. Pairs with unique concordant alignments
		itoa10<uint64_t>(rpmu.nconcord_uni, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 11. Pairs with repetitive concordant alignments
		itoa10<uint64_t>(rpmu.nconcord_rep, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 12. Pairs with 0 concordant alignments
		itoa10<uint64_t>(rpmu.nconcord_0, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 13. Pairs with 1 discordant alignment
		itoa10<uint64_t>(rpmu.ndiscord, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 14. Mates from unaligned pairs that align uniquely
		itoa10<uint64_t>(rpmu.nunp_0_uni, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 15. Mates from unaligned pairs that align repetitively
		itoa10<uint64_t>(rpmu.nunp_0_rep, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 16. Mates from unaligned pairs that fail to align
		itoa10<uint64_t>(rpmu.nunp_0_0, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 17. Mates from repetitive pairs that align uniquely
		itoa10<uint64_t>(rpmu.nunp_rep_uni, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 18. Mates from repetitive pairs that align repetitively
		itoa10<uint64_t>(rpmu.nunp_rep_rep, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 19. Mates from repetitive pairs that fail to align
		itoa10<uint64_t>(rpmu.nunp_rep_0, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 20. Unpaired reads that align uniquely
		itoa10<uint64_t>(rpmu.nunp_uni, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 21. Unpaired reads that align repetitively
		itoa10<uint64_t>(rpmu.nunp_rep, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 22. Unpaired reads that fail to align
		itoa10<uint64_t>(rpmu.nunp_0, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }

		// 23. Seed searches
		itoa10<uint64_t>(sdmu.seedsearch, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 24. Hits in 'current' cache
		itoa10<uint64_t>(sdmu.intrahit, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 25. Hits in 'local' cache
		itoa10<uint64_t>(sdmu.interhit, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 26. Out of memory
		itoa10<uint64_t>(sdmu.ooms, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 27. Burrows-Wheeler ops in aligner
		itoa10<uint64_t>(sdmu.bwops, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 28. Burrows-Wheeler branches (edits) in aligner
		itoa10<uint64_t>(sdmu.bweds, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 29. Burrows-Wheeler ops in resolver
		itoa10<uint64_t>(wlmu.bwops, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 30. Burrows-Wheeler branches in resolver
		itoa10<uint64_t>(wlmu.branches, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 31. Burrows-Wheeler offset resolutions
		itoa10<uint64_t>(wlmu.resolves, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 32. Reference-scanner hits
		itoa10<uint64_t>(wlmu.refscanhits, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 33. Reference-scanning offset resolutions
		itoa10<uint64_t>(wlmu.refresolves, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 34. Offset reports
		itoa10<uint64_t>(wlmu.reports, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		
		// 35. Redundant seed hit
		itoa10<uint64_t>(swmuSeed.rshit, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		
		// 36. 16-bit SSE seed-extend DPs tried
		itoa10<uint64_t>(dpSse16uSeed.dp, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 37. 16-bit SSE seed-extend DPs saturated
		itoa10<uint64_t>(dpSse16uSeed.dpsat, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 38. 16-bit SSE seed-extend DPs failed
		itoa10<uint64_t>(dpSse16uSeed.dpfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 39. 16-bit SSE seed-extend DPs succeeded
		itoa10<uint64_t>(dpSse16uSeed.dpsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 40. 16-bit SSE seed-extend DP columns completed
		itoa10<uint64_t>(dpSse16uSeed.col, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 41. 16-bit SSE seed-extend DP cells completed
		itoa10<uint64_t>(dpSse16uSeed.cell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 42. 16-bit SSE seed-extend DP inner loop iters completed
		itoa10<uint64_t>(dpSse16uSeed.inner, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 43. 16-bit SSE seed-extend DP fixup loop iters completed
		itoa10<uint64_t>(dpSse16uSeed.fixup, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 44. 16-bit SSE seed-extend DP gather, cells examined
		itoa10<uint64_t>(dpSse16uSeed.gathcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 45. 16-bit SSE seed-extend DP gather, cells with potential solutions
		itoa10<uint64_t>(dpSse16uSeed.gathsol, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 46. 16-bit SSE seed-extend DP backtrace attempts
		itoa10<uint64_t>(dpSse16uSeed.bt, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 47. 16-bit SSE seed-extend DP failed backtrace attempts
		itoa10<uint64_t>(dpSse16uSeed.btfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 48. 16-bit SSE seed-extend DP succesful backtrace attempts
		itoa10<uint64_t>(dpSse16uSeed.btsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 49. 16-bit SSE seed-extend DP backtrace cells
		itoa10<uint64_t>(dpSse16uSeed.btcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		
		// 50. 8-bit SSE seed-extend DPs tried
		itoa10<uint64_t>(dpSse8uSeed.dp, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 51. 8-bit SSE seed-extend DPs saturated
		itoa10<uint64_t>(dpSse8uSeed.dpsat, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 52. 8-bit SSE seed-extend DPs failed
		itoa10<uint64_t>(dpSse8uSeed.dpfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 53. 8-bit SSE seed-extend DPs succeeded
		itoa10<uint64_t>(dpSse8uSeed.dpsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 54. 8-bit SSE seed-extend DP columns completed
		itoa10<uint64_t>(dpSse8uSeed.col, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 55. 8-bit SSE seed-extend DP cells completed
		itoa10<uint64_t>(dpSse8uSeed.cell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 56. 8-bit SSE seed-extend DP inner loop iters completed
		itoa10<uint64_t>(dpSse8uSeed.inner, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 57. 8-bit SSE seed-extend DP fixup loop iters completed
		itoa10<uint64_t>(dpSse8uSeed.fixup, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 58. 16-bit SSE seed-extend DP gather, cells examined
		itoa10<uint64_t>(dpSse8uSeed.gathcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 59. 16-bit SSE seed-extend DP gather, cells with potential solutions
		itoa10<uint64_t>(dpSse8uSeed.gathsol, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 60. 16-bit SSE seed-extend DP backtrace attempts
		itoa10<uint64_t>(dpSse8uSeed.bt, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 61. 16-bit SSE seed-extend DP failed backtrace attempts
		itoa10<uint64_t>(dpSse8uSeed.btfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 62. 16-bit SSE seed-extend DP succesful backtrace attempts
		itoa10<uint64_t>(dpSse8uSeed.btsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 63. 16-bit SSE seed-extend DP backtrace cells
		itoa10<uint64_t>(dpSse8uSeed.btcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		
		// 64. 16-bit SSE mate-finding DPs tried
		itoa10<uint64_t>(dpSse16uMate.dp, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 65. 16-bit SSE mate-finding DPs saturated
		itoa10<uint64_t>(dpSse16uMate.dpsat, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 66. 16-bit SSE mate-finding DPs failed
		itoa10<uint64_t>(dpSse16uMate.dpfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 67. 16-bit SSE mate-finding DPs succeeded
		itoa10<uint64_t>(dpSse16uMate.dpsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 68. 16-bit SSE mate-finding DP columns completed
		itoa10<uint64_t>(dpSse16uMate.col, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 69. 16-bit SSE mate-finding DP cells completed
		itoa10<uint64_t>(dpSse16uMate.cell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 70. 16-bit SSE mate-finding DP inner loop iters completed
		itoa10<uint64_t>(dpSse16uMate.inner, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 71. 16-bit SSE mate-finding DP fixup loop iters completed
		itoa10<uint64_t>(dpSse16uMate.fixup, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 72. 16-bit SSE mate-finding DP gather, cells examined
		itoa10<uint64_t>(dpSse16uMate.gathcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 73. 16-bit SSE mate-finding DP gather, cells with potential solutions
		itoa10<uint64_t>(dpSse16uMate.gathsol, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 74. 16-bit SSE mate-finding DP backtrace attempts
		itoa10<uint64_t>(dpSse16uMate.bt, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 75. 16-bit SSE mate-finding DP failed backtrace attempts
		itoa10<uint64_t>(dpSse16uMate.btfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 76. 16-bit SSE mate-finding DP succesful backtrace attempts
		itoa10<uint64_t>(dpSse16uMate.btsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 77. 16-bit SSE mate-finding DP backtrace cells
		itoa10<uint64_t>(dpSse16uMate.btcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		
		// 78. 8-bit SSE mate-finding DPs tried
		itoa10<uint64_t>(dpSse8uMate.dp, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 79. 8-bit SSE mate-finding DPs saturated
		itoa10<uint64_t>(dpSse8uMate.dpsat, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 80. 8-bit SSE mate-finding DPs failed
		itoa10<uint64_t>(dpSse8uMate.dpfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 81. 8-bit SSE mate-finding DPs succeeded
		itoa10<uint64_t>(dpSse8uMate.dpsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 82. 8-bit SSE mate-finding DP columns completed
		itoa10<uint64_t>(dpSse8uMate.col, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 83. 8-bit SSE mate-finding DP cells completed
		itoa10<uint64_t>(dpSse8uMate.cell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 84. 8-bit SSE mate-finding DP inner loop iters completed
		itoa10<uint64_t>(dpSse8uMate.inner, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 85. 8-bit SSE mate-finding DP fixup loop iters completed
		itoa10<uint64_t>(dpSse8uMate.fixup, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 86. 16-bit SSE mate-finding DP gather, cells examined
		itoa10<uint64_t>(dpSse8uMate.gathcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 87. 16-bit SSE mate-finding DP gather, cells with potential solutions
		itoa10<uint64_t>(dpSse8uMate.gathsol, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 88. 16-bit SSE mate-finding DP backtrace attempts
		itoa10<uint64_t>(dpSse8uMate.bt, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 89. 16-bit SSE mate-finding DP failed backtrace attempts
		itoa10<uint64_t>(dpSse8uMate.btfail, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 90. 16-bit SSE mate-finding DP succesful backtrace attempts
		itoa10<uint64_t>(dpSse8uMate.btsucc, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 91. 16-bit SSE mate-finding DP backtrace cells
		itoa10<uint64_t>(dpSse8uMate.btcell, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
				
		// 92. Overall memory peak
		itoa10<size_t>(gMemTally.peak() >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 93. Uncategorized memory peak
		itoa10<size_t>(gMemTally.peak(0) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 94. Ebwt memory peak
		itoa10<size_t>(gMemTally.peak(EBWT_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 95. Cache memory peak
		itoa10<size_t>(gMemTally.peak(CA_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 96. Resolver memory peak
		itoa10<size_t>(gMemTally.peak(GW_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 97. Seed aligner memory peak
		itoa10<size_t>(gMemTally.peak(AL_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 98. Dynamic programming aligner memory peak
		itoa10<size_t>(gMemTally.peak(DP_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 99. Miscellaneous memory peak
		itoa10<size_t>(gMemTally.peak(MISC_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf << '\t';
		if(o != NULL) { o->writeChars(buf); o->write('\t'); }
		// 100. Debug memory peak
		itoa10<size_t>(gMemTally.peak(DEBUG_CAT) >> 20, buf);
		if(metricsStderr) cerr << buf;
		if(o != NULL) { o->writeChars(buf); }

		if(o != NULL) { o->write('\n'); }
		if(metricsStderr) cerr << endl;
		finishReport();
	}
	
	void finishReport() {
		olm.merge(olmu, false);
		sdm.merge(sdmu, false);
		wlm.merge(wlmu, false);
		swmSeed.merge(swmuSeed, false);
		swmMate.merge(swmuMate, false);
		dpSse8Seed.merge(dpSse8uSeed, false);
		dpSse8Mate.merge(dpSse8uMate, false);
		dpSse16Seed.merge(dpSse16uSeed, false);
		dpSse16Mate.merge(dpSse16uMate, false);
		olmu.reset();
		sdmu.reset();
		wlmu.reset();
		swmuSeed.reset();
		swmuMate.reset();
		rpmu.reset();
		dpSse8uSeed.reset();
		dpSse8uMate.reset();
		dpSse16uSeed.reset();
		dpSse16uMate.reset();
	}

	// Total over the whole job
	OuterLoopMetrics  olm;   // overall metrics
	SeedSearchMetrics sdm;   // metrics related to seed alignment
	WalkMetrics       wlm;   // metrics related to walking left (i.e. resolving reference offsets)
	SwMetrics         swmSeed;  // metrics related to DP seed-extend alignment
	SwMetrics         swmMate;  // metrics related to DP mate-finding alignment
	ReportingMetrics  rpm;   // metrics related to reporting
	SSEMetrics        dpSse8Seed;  // 8-bit SSE seed extensions
	SSEMetrics        dpSse8Mate;    // 8-bit SSE mate finds
	SSEMetrics        dpSse16Seed; // 16-bit SSE seed extensions
	SSEMetrics        dpSse16Mate;   // 16-bit SSE mate finds

	// Just since the last update
	OuterLoopMetrics  olmu;  // overall metrics
	SeedSearchMetrics sdmu;  // metrics related to seed alignment
	WalkMetrics       wlmu;  // metrics related to walking left (i.e. resolving reference offsets)
	SwMetrics         swmuSeed; // metrics related to DP seed-extend alignment
	SwMetrics         swmuMate; // metrics related to DP mate-finding alignment
	ReportingMetrics  rpmu;  // metrics related to reporting
	SSEMetrics        dpSse8uSeed;  // 8-bit SSE seed extensions
	SSEMetrics        dpSse8uMate;  // 8-bit SSE mate finds
	SSEMetrics        dpSse16uSeed; // 16-bit SSE seed extensions
	SSEMetrics        dpSse16uMate; // 16-bit SSE mate finds

	MUTEX_T           lock;  // lock for when one ob
	bool              first; // yet to print first line?
	time_t            lastElapsed; // used in reportInterval to measure time since last call
};

static PerfMetrics metrics;

// Cyclic rotations
#define ROTL(n, x) (((x) << (n)) | ((x) >> (32-n)))
#define ROTR(n, x) (((x) >> (n)) | ((x) << (32-n)))

static inline void printMmsSkipMsg(
	const PatternSourcePerThread& ps,
	bool paired,
	bool mate1,
	int seedmms)
{
	if(paired) {
		cerr << "Warning: skipping mate #" << (mate1 ? '1' : '2')
		     << " of read '" << (mate1 ? ps.bufa().name : ps.bufb().name)
		     << "' because length (" << (mate1 ? ps.bufa().patFw.length() :
			                                     ps.bufb().patFw.length())
			 << ") <= # seed mismatches (" << seedmms << ")" << endl;
	} else {
		cerr << "Warning: skipping read '" << (mate1 ? ps.bufa().name : ps.bufb().name)
		     << "' because length (" << (mate1 ? ps.bufa().patFw.length() :
			                                     ps.bufb().patFw.length())
			 << ") <= # seed mismatches (" << seedmms << ")" << endl;
	}
}

static inline void printColorLenSkipMsg(
	const PatternSourcePerThread& ps,
	bool paired,
	bool mate1)
{
	if(paired) {
		cerr << "Warning: skipping mate #" << (mate1 ? '1' : '2')
		     << " of read '" << (mate1 ? ps.bufa().name : ps.bufb().name)
		     << "' because it was colorspace, --col-keepends was not "
			 << "specified, and length was < 2" << endl;
	} else {
		cerr << "Warning: skipping read '" << (mate1 ? ps.bufa().name : ps.bufb().name)
		     << "' because it was colorspace, --col-keepends was not "
			 << "specified, and length was < 2" << endl;
	}
}

#define MERGE_METRICS() { \
	msink.mergeMetrics(rpm); \
	metrics.merge( \
		&olm, \
		&sdm, \
		&wlm, \
		&swmSeed, \
		&swmMate, \
		&rpm, \
		&sseU8ExtendMet, \
		&sseU8MateMet, \
		&sseI16ExtendMet, \
		&sseI16MateMet, \
		nthreads > 1); \
	olm.reset(); \
	sdm.reset(); \
	wlm.reset(); \
	swmSeed.reset(); \
	swmMate.reset(); \
	rpm.reset(); \
	sseU8ExtendMet.reset(); \
	sseU8MateMet.reset(); \
	sseI16ExtendMet.reset(); \
	sseI16MateMet.reset(); \
}

/**
 * Called once per thread.  Sets up per-thread pointers to the shared global
 * data structures, creates per-thread structures, then enters the alignment
 * loop.  The general flow of the alignment loop is:
 *
 * - If it's been a while and we're the master thread, report some alignment
 *   metrics
 * - Get the next read/pair
 * - Check if this read/pair is identical to the previous
 *   + If identical, check whether we can skip any or all alignment stages.  If
 *     we can skip all stages, report the result immediately and move to next
 *     read/pair
 *   + If not identical, continue
 * - 
 */
static void* multiseedSearchWorker(void *vp) {
	int tid = *((int*)vp);
	assert(multiseed_ebwtFw != NULL);
	assert(multiseedMms == 0 || multiseed_ebwtBw != NULL);
	PairedPatternSource&    patsrc   = *multiseed_patsrc;
	const Ebwt&             ebwtFw   = *multiseed_ebwtFw;
	const Ebwt&             ebwtBw   = *multiseed_ebwtBw;
	const Scoring&          sc       = *multiseed_sc;
	const EList<Seed>&      seeds    = *multiseed_seeds;
	const BitPairReference& ref      = *multiseed_refs;
	AlignmentCache&         scShared = *multiseed_ca;
	AlnSink&                msink    = *multiseed_msink;
	OutFileBuf*             metricsOfb = multiseed_metricsOfb;

	// Sinks: these are so that we can print tables encoding counts for
	// events of interest on a per-read, per-seed, per-join, or per-SW
	// level.  These in turn can be used to diagnose performance
	// problems, or generally characterize performance.
	
	// readCounterSink: for each read, keep a record of counts related
	// to all phases of alignment: seed alignment, joining, SW
	EList<ReadCounterSink*>& readCounterSink = *multiseed_readCounterSink;

	// seedHitSink: for each seed hit, keep a record of counts
	EList<SeedHitSink*>&     seedHitSink     = *multiseed_seedHitSink;
	// seedCounterSink: for each seed, keep a record of counts related
	// to the seed alignment
	EList<SeedCounterSink*>& seedCounterSink = *multiseed_seedCounterSink;
	// seedActionSink: keep a detailed record of seed alignment actions
	// taken per read
	EList<SeedActionSink*>&  seedActionSink  = *multiseed_seedActionSink;

	// swCounterSink: for each SW, keep a record of counts related to
	// the SW alignment
	EList<SwCounterSink*>&   swCounterSink   = *multiseed_swCounterSink;
	// swActionSink: keep a detailed record of SW alignment actions
	// taken per read
	EList<SwActionSink*>&    swActionSink    = *multiseed_swActionSink;
	
	//const BitPairReference& refs   = *multiseed_refs;
	auto_ptr<PatternSourcePerThreadFactory> patsrcFact(createPatsrcFactory(patsrc, tid));
	auto_ptr<PatternSourcePerThread> ps(patsrcFact->create());
	
	// Thread-local cache for seed alignments
	PtrWrap<AlignmentCache> scLocal;
	if(!msNoCache) {
		scLocal.init(new AlignmentCache(seedCacheLocalMB * 1024 * 1024, false));
	}
	AlignmentCache scCurrent(seedCacheCurrentMB * 1024 * 1024, false);
	// Thread-local cache for current seed alignments
	
	// Interfaces for alignment and seed caches
	AlignmentCacheIface ca(
		&scCurrent,
		scLocal.get(),
		msNoCache ? NULL : &scShared);
	
	// Instantiate an object for holding reporting-related parameters.
	ReportingParams rp(
		(allHits ? std::numeric_limits<THitInt>::max() : khits), // -k
		mhits,             // -m/-M
		0,                 // penalty gap (not used now)
		msample,           // true -> -M was specified, otherwise assume -m
		gReportDiscordant, // report discordang paired-end alignments?
		gReportMixed);     // report unpaired alignments for paired reads?
	
	// Given user-specified ROWM & POSF thresholds and user settings for
	// -k/-a/-M, multiply the ROWM and POSF settings by the minimum number
	// of alignments we're seeking.  E.g. for -M 1 or -k 2, multiply by 2.
	// For -M 10, multiply by 11, etc.
	SimpleFunc myPosfrac = posfrac;
	SimpleFunc myRowmult = rowmult;
	rp.boostThresholds(myPosfrac, myRowmult);
	
	// Instantiate a mapping quality calculator
	auto_ptr<Mapq> bmapq(new BowtieMapq(
		scoreMin,
		mapqTopCoeff,
		mapqBotCoeff,
		mapqMax,
		sc));
	
	// Make a per-thread wrapper for the global MHitSink object.
	AlnSinkWrap msinkwrap(msink, rp, *bmapq.get());

	SeedAligner al;
	SwDriver sd;
	SwAligner sw(!noSse), osw(!noSse);
	SeedResults shs[2];
	QVal *qv;
	OuterLoopMetrics olm;
	SeedSearchMetrics sdm;
	WalkMetrics wlm;
	SwMetrics swmSeed, swmMate;
	ReportingMetrics rpm;
	RandomSource rnd;
	SSEMetrics sseU8ExtendMet;
	SSEMetrics sseU8MateMet;
	SSEMetrics sseI16ExtendMet;
	SSEMetrics sseI16MateMet;

	ASSERT_ONLY(BTDnaString tmp);

	int pepolFlag;
	if(gMate1fw && gMate2fw) {
		pepolFlag = PE_POLICY_FF;
	} else if(gMate1fw && !gMate2fw) {
		pepolFlag = PE_POLICY_FR;
	} else if(!gMate1fw && gMate2fw) {
		pepolFlag = PE_POLICY_RF;
	} else {
		pepolFlag = PE_POLICY_RR;
	}
	assert_geq(gMaxInsert, gMinInsert);
	assert_geq(gMinInsert, 0);
	PairedEndPolicy pepol(
		pepolFlag,
		gMaxInsert,
		gMinInsert,
		localAlign,
		gFlippedMatesOK,
		gDovetailMatesOK,
		gContainMatesOK,
		gOlapMatesOK,
		gExpandToFrag);
	
	// Used by thread with threadid == 1 to measure time elapsed
	time_t iTime = time(0);

	// Keep track of whether last search was exhaustive for mates 1 and 2
	bool exhaustive[2] = { false, false };
	// Keep track of whether mates 1/2 were filtered out last time through
	bool filt[2]    = { true, true };
	// Keep track of whether mates 1/2 were filtered out due Ns last time
	bool nfilt[2]   = { true, true };
	// Keep track of whether mates 1/2 were filtered out due to not having
	// enough characters to rise about the score threshold.
	bool scfilt[2]  = { true, true };
	// Keep track of whether mates 1/2 were filtered out due to not having
	// more characters than the number of mismatches permitted in a seed.
	bool lenfilt[2] = { true, true };
	// Keep track of whether mates 1/2 were filtered out by upstream qc
	bool qcfilt[2]  = { true, true };

	int mergei = 0;
	int mergeival = 16;
	while(true) {
		bool success = false, done = false, paired = false;
		ps->nextReadPair(success, done, paired, outType != OUTPUT_SAM);
		if(!success && done) {
			break;
		} else if(!success) {
			continue;
		}
		TReadId patid = ps->patid();
		if(patid >= skipReads && patid < qUpto) {
			// Align this read/pair
			bool retry = true;
			if(metricsIval > 0 &&
			   (metricsOfb != NULL || metricsStderr) &&
			   ++mergei == mergeival)
			{
				// Update global metrics, in a synchronized manner if needed
				MERGE_METRICS();
				mergei = 0;
				// Check if a progress message should be printed
				if(tid == 0) {
					// Only thread 1 prints progress messages
					time_t curTime = time(0);
					if(curTime - iTime >= metricsIval) {
						metrics.reportInterval(metricsOfb, metricsStderr, true);
						iTime = curTime;
					}
				}
			}
			while(retry) {
				qv = NULL;
				retry = false;
				assert_eq(ps->bufa().color, gColor);
				ca.nextRead();
				olm.reads++;
				assert(!ca.aligning());
				// NB: read may be either unpaired or paired-end at this point
				bool pair = paired;
				const size_t rdlen1 = ps->bufa().length();
				const size_t rdlen2 = pair ? ps->bufb().length() : 0;
				olm.bases += (rdlen1 + rdlen2);
				// Check if read is identical to previous read
				rnd.init(ROTL(ps->bufa().seed, 5));
				int skipStages = msinkwrap.nextRead(
					&ps->bufa(),
					pair ? &ps->bufb() : NULL,
					patid,
					sc.qualitiesMatter());
				assert(msinkwrap.inited());
				if(skipStages == -1) {
					// Read or pair is identical to previous.  Re-report from
					// the msinkwrap immediately.
					olm.srreads++;
					olm.srbases += (rdlen1 + rdlen2);
					rnd.init(ROTL(ps->bufa().seed, 20));
					msinkwrap.finishRead(
						NULL,                 // seed results for mate 1
						NULL,                 // seed results for mate 2
						exhaustive[0],        // exhausted seed results for 1?
						exhaustive[1],        // exhausted seed results for 2?
						nfilt[0],
						nfilt[1],
						scfilt[0],
						scfilt[1],
						lenfilt[0],
						lenfilt[1],
						qcfilt[0],
						qcfilt[1],
						sortByScore,          // prioritize by alignment score
						rnd,                  // pseudo-random generator
						rpm,                  // reporting metrics
						!seedSummaryOnly,     // suppress seed summaries?
						seedSummaryOnly);     // suppress alignments?
					break; // next read
				}
				size_t rdlens[2] = { rdlen1, rdlen2 };
				size_t rdrows[2] = { rdlen1, rdlen2 };
				if(gColor) {
					rdrows[0]++;
					if(rdrows[1] > 0) {
						rdrows[1]++;
					}
				}
				// Calculate the minimum valid score threshold for the read
				TAlScore minsc[2];
				if(bwaSwLike) {
					// From BWA-SW manual: "Given an l-long query, the
					// threshold for a hit to be retained is
					// a*max{T,c*log(l)}."  We try to recreate that here.
					float a = (float)sc.match(30);
					float T = bwaSwLikeT, c = bwaSwLikeC;
					minsc[0] = (TAlScore)max<float>(a*T, a*c*log(rdlens[0]));
					if(paired) {
						minsc[1] = (TAlScore)max<float>(a*T, a*c*log(rdlens[1]));
					}
				} else {
					minsc[0] = scoreMin.f<TAlScore>(rdlens[0]);
					if(paired) {
						minsc[1] = scoreMin.f<TAlScore>(rdlens[1]);
					}
				}
				// Calculate the local-alignment score floor for the read
				TAlScore floorsc[2];
				if(localAlign) {
					floorsc[0] = scoreFloor.f<TAlScore>(rdlens[0]);
					if(paired) {
						floorsc[1] = scoreFloor.f<TAlScore>(rdlens[1]);
					}
				} else {
					floorsc[0] = floorsc[1] = std::numeric_limits<TAlScore>::min();
				}
				// N filter; does the read have too many Ns?
				sc.nFilterPair(
					&ps->bufa().patFw,
					pair ? &ps->bufb().patFw : NULL,
					nfilt[0],
					nfilt[1]);
				// Score filter; does the read enough character to rise above
				// the score threshold?
				scfilt[0] = sc.scoreFilter(minsc[0], rdlens[0]);
				scfilt[1] = sc.scoreFilter(minsc[1], rdlens[1]);
				lenfilt[0] = lenfilt[1] = true;
				if(rdlens[0] <= (size_t)multiseedMms) {
					if(!gQuiet) {
						printMmsSkipMsg(*ps, paired, true, multiseedMms);
					}
					lenfilt[0] = false;
				}
				if(rdlens[1] <= (size_t)multiseedMms && paired) {
					if(!gQuiet) {
						printMmsSkipMsg(*ps, paired, false, multiseedMms);
					}
					lenfilt[1] = false;
				}
				if(gColor && gColorExEnds) {
					if(rdlens[0] < 2) {
						printColorLenSkipMsg(*ps, paired, true);
						lenfilt[0] = false;
					}
					if(rdlens[1] < 2 && paired) {
						printColorLenSkipMsg(*ps, paired, false);
						lenfilt[1] = false;
					}
				}
				qcfilt[0] = qcfilt[1] = true;
				if(qcFilter) {
					qcfilt[0] = (ps->bufa().filter != '0');
					qcfilt[1] = (ps->bufb().filter != '0');
				}
				filt[0] = (nfilt[0] && scfilt[0] && lenfilt[0] && qcfilt[0]);
				filt[1] = (nfilt[1] && scfilt[1] && lenfilt[1] && qcfilt[1]);
				const Read* rds[2] = { &ps->bufa(), &ps->bufb() };
				// For each mate...
				assert(msinkwrap.empty());
				sd.nextRead(paired, rdrows[0], rdrows[1]);
				bool matemap[2] = { 0, 1 };
				if(pair) {
					rnd.init(ROTL(rds[0]->seed ^ rds[1]->seed, 10));
					if(rnd.nextU2() == 0) {
						// Swap order in which mates are investigated
						std::swap(matemap[0], matemap[1]);
					}
				}
				exhaustive[0] = exhaustive[1] = false;
				for(size_t matei = 0; matei < 2; matei++) {
					size_t mate = matemap[matei];
					if(!filt[mate]) {
						// Mate was rejected by N filter
						olm.freads++;               // reads filtered out
						olm.fbases += rdlens[mate]; // bases filtered out
						continue; // on to next mate
					} else {
						olm.ureads++;               // reads passing filter
						olm.ubases += rdlens[mate]; // bases passing filter
					}
					if(msinkwrap.state().doneWithMate(mate == 0)) {
						// Done with this mate
						continue;
					}
					assert_geq(rds[mate]->length(), 0);
					assert(!msinkwrap.maxed());
					assert(msinkwrap.repOk());
					QKey qkr(rds[mate]->patFw ASSERT_ONLY(, tmp));
					rnd.init(ROTL(rds[mate]->seed, 10));
					// Seed search
					shs[mate].clear(); // clear seed hits
					assert(shs[mate].repOk(&ca.current()));
					// Calculate the seed interval as a
					// function of the read length
					int interval = msIval.f<int>((double)rdlens[mate]);
					if(interval < 1) interval = 1;
					assert_geq(interval, 1);
					// Set flags controlling which orientations of  individual
					// mates to investigate
					bool nofw, norc;
					if(paired && mate == 0) {
						// Mate #1
						nofw = (gMate1fw ? gNofw : gNorc);
						norc = (gMate1fw ? gNorc : gNofw);
					} else if(paired && mate == 1) {
						// Mate #2
						nofw = (gMate2fw ? gNofw : gNorc);
						norc = (gMate2fw ? gNorc : gNofw);
					} else {
						// Unpaired
						nofw = gNofw;
						norc = gNorc;
					}
					// Instantiate the seeds
					std::pair<int, int> inst = al.instantiateSeeds(
						seeds,       // search seeds
						interval,    // interval between seeds
						*rds[mate],  // read to align
						sc,          // scoring scheme
						nofw,        // don't align forward read
						norc,        // don't align revcomp read
						ca,          // holds some seed hits from previous reads
						shs[mate],   // holds all the seed hits
						sdm);        // metrics
					assert(shs[mate].repOk(&ca.current()));
					if(inst.first + inst.second == 0) {
						continue; // on to next mate
					}
					// Align seeds
					al.searchAllSeeds(
						seeds,            // search seeds
						&ebwtFw,          // BWT index
						&ebwtBw,          // BWT' index
						*rds[mate],       // read
						sc,               // scoring scheme
						ca,               // alignment cache
						shs[mate],        // store seed hits here
						sdm,              // metrics
						&readCounterSink, // send counter summary for each read to this sink
						&seedHitSink,     // send seed hits to this sink
						&seedCounterSink, // send counter summary for each seed to this sink
						&seedActionSink); // send search action list for each read to this sink
					assert_eq(0, sdm.ooms);
					assert(shs[mate].repOk(&ca.current()));
					if(!seedSummaryOnly) {
						// If there aren't any seed hits...
						if(shs[mate].empty()) {
							continue; // on to the next mate
						}
						// Sort seed hits into ranks
						shs[mate].rankSeedHits(rnd);
						int nceil = nCeil.f<int>((double)rdlens[mate]);
						nceil = min(nceil, (int)rdlens[mate]);
						bool done = false;
						if(pair) {
							int onceil = nCeil.f<int>((double)rdlens[mate ^ 1]);
							onceil = min(onceil, (int)rdlens[mate ^ 1]);
							// Paired-end dynamic programming driver
							done = sd.extendSeedsPaired(
								*rds[mate],     // mate to align as anchor
								*rds[mate ^ 1], // mate to align as opp.
								mate == 0,      // anchor is mate 1?
								!filt[mate ^ 1],// opposite mate filtered out?
								gColor,         // colorspace?
								shs[mate],      // seed hits for anchor
								ebwtFw,         // bowtie index
								ref,            // packed reference strings
								sw,             // dyn prog aligner, anchor
								osw,            // dyn prog aligner, opposite
								sc,             // scoring scheme
								pepol,          // paired-end policy
								multiseedMms,   // # mms allowed in a seed
								multiseedLen,   // length of a seed
								interval,       // interval between seeds
								minsc[mate],    // min score for anchor
								minsc[mate^1],  // min score for opp.
								floorsc[mate],  // floor score for anchor
								floorsc[mate^1],// floor score for opp.
								nceil,          // N ceil for anchor
								onceil,         // N ceil for opp.
								nofw,           // don't align forward read
								norc,           // don't align revcomp read
								myPosfrac,      // max seed poss to try
								myRowmult,      // extensions per pos
								maxhalf,        // max width on one DP side
								scanNarrowed,   // ref scan narrowed seed hits?
								ca,             // seed alignment cache
								rnd,            // pseudo-random source
								wlm,            // group walk left metrics
								swmSeed,        // DP metrics, seed extend
								swmMate,        // DP metrics, mate finding
								&msinkwrap,     // for organizing hits
								true,           // seek mate immediately
								true,           // report hits once found
								gReportDiscordant,// look for discordant alns?
								gReportMixed,   // look for unpaired alns?
								&swCounterSink, // send counter info here
								&swActionSink,  // send action info here
								exhaustive[mate]);
							// Might be done, but just with this mate
						} else {
							// Unpaired dynamic programming driver
							done = sd.extendSeeds(
								*rds[mate],     // read
								mate == 0,      // mate #1?
								gColor,         // colorspace?
								shs[mate],      // seed hits
								ebwtFw,         // bowtie index
								ref,            // packed reference strings
								sw,             // dynamic prog aligner
								sc,             // scoring scheme
								multiseedMms,   // # mms allowed in a seed
								multiseedLen,   // length of a seed
								interval,       // interval between seeds
								minsc[mate],    // minimum score for valid
								floorsc[mate],  // floor score
								nceil,          // N ceil for anchor
								myPosfrac,      // additional seed poss to try
								myRowmult,      // extensions per pos
								maxhalf,        // max width on one DP side
								scanNarrowed,   // ref scan narrowed seed hits?
								ca,             // seed alignment cache
								rnd,            // pseudo-random source
								wlm,            // group walk left metrics
								swmSeed,        // DP metrics, seed extend
								&msinkwrap,     // for organizing hits
								true,           // report hits once found
								&swCounterSink, // send counter info here
								&swActionSink,  // send action info here
								exhaustive[mate]);
						}
						sw.merge(
							sseU8ExtendMet,  // metrics for SSE 8-bit seed extends
							sseU8MateMet,    // metrics for SSE 8-bit mate finding
							sseI16ExtendMet, // metrics for SSE 16-bit seed extends
							sseI16MateMet);  // metrics for SSE 16-bit mate finding
						sw.resetCounters();
						osw.merge(
							sseU8ExtendMet,  // metrics for SSE 8-bit seed extends
							sseU8MateMet,    // metrics for SSE 8-bit mate finding
							sseI16ExtendMet, // metrics for SSE 16-bit seed extends
							sseI16MateMet);  // metrics for SSE 16-bit mate finding
						osw.resetCounters();
						// Are we done with this read/pair?
						if(done) {
							break; // ...break out of the loop over mates
						}
					} // if(!seedSummaryOnly)
				} // for(size_t matei = 0; matei < 2; matei++)
				
				// Commit and report paired-end/unpaired alignments
				uint32_t seed = rds[0]->seed ^ rds[1]->seed;
				rnd.init(ROTL(seed, 20));
				msinkwrap.finishRead(
					&shs[0],              // seed results for mate 1
					&shs[1],              // seed results for mate 2
					exhaustive[0],        // exhausted seed hits for mate 1?
					exhaustive[1],        // exhausted seed hits for mate 2?
					nfilt[0],
					nfilt[1],
					scfilt[0],
					scfilt[1],
					lenfilt[0],
					lenfilt[1],
					qcfilt[0],
					qcfilt[1],
					sortByScore,          // prioritize by alignment score
					rnd,                  // pseudo-random generator
					rpm,                  // reporting metrics
					!seedSummaryOnly,     // suppress seed summaries?
					seedSummaryOnly);     // suppress alignments?
				assert(!retry || msinkwrap.empty());
			} // while(retry)
		} // if(patid >= skipReads && patid < qUpto)
		else if(patid >= qUpto) {
			break;
		}
	} // while(true)
	
	// One last metrics merge, in a synchronized manner if needed
	MERGE_METRICS();
#ifdef BOWTIE_PTHREADS
	if(tid > 0) { pthread_exit(NULL); }
#endif
	return NULL;
}

/**
 * Called once per alignment job.  Sets up global pointers to the
 * shared global data structures, creates per-thread structures, then
 * enters the search loop.
 */
static void multiseedSearch(
	Scoring& sc,
	EList<Seed>& seeds,
	PairedPatternSource& patsrc,  // pattern source
	AlnSink& msink,             // hit sink
	Ebwt& ebwtFw,                 // index of original text
	Ebwt& ebwtBw,                 // index of mirror text
	EList<SeedHitSink*>& seedHitSink,
	EList<SeedCounterSink*>& seedCounterSink,
	EList<SeedActionSink*>&  seedActionSink,
	OutFileBuf *metricsOfb)
{
	multiseed_patsrc = &patsrc;
	multiseed_msink  = &msink;
	multiseed_ebwtFw = &ebwtFw;
	multiseed_ebwtBw = &ebwtBw;
	multiseed_sc     = &sc;
	multiseed_seeds  = &seeds;
	multiseed_metricsOfb      = metricsOfb;
	multiseed_seedHitSink     = &seedHitSink;
	multiseed_seedCounterSink = &seedCounterSink;
	multiseed_seedActionSink  = &seedActionSink;
	Timer *_t = new Timer(cerr, "Time loading reference: ", timing);
	auto_ptr<BitPairReference> refs(
		new BitPairReference(
			adjIdxBase,
			gColor,
			sanityCheck,
			NULL,
			NULL,
			false,
			useMm,
			useShmem,
			mmSweep,
			gVerbose,
			startVerbose)
	);
	delete _t;
	if(!refs->loaded()) throw 1;
	multiseed_refs = refs.get();
#ifdef BOWTIE_PTHREADS
	AutoArray<pthread_t> threads(nthreads-1);
	AutoArray<int> tids(nthreads-1);
#endif
	{
		// Load the other half of the index into memory
		assert(!ebwtFw.isInMemory());
		Timer _t(cerr, "Time loading forward index: ", timing);
		ebwtFw.loadIntoMemory(
			gColor ? 1 : 0, // colorspace?
			-1, // not the reverse index
			true,         // load SA samp? (yes, need forward index's SA samp)
			true,         // load ftab (in forward index)
			true,         // load rstarts (in forward index)
			!noRefNames,  // load names?
			startVerbose);
	}
	if(multiseedMms > 0) {
		// Load the other half of the index into memory
		assert(!ebwtBw.isInMemory());
		Timer _t(cerr, "Time loading mirror index: ", timing);
		ebwtBw.loadIntoMemory(
			gColor ? 1 : 0, // colorspace?
			// It's bidirectional search, so we need the reverse to be
			// constructed as the reverse of the concatenated strings.
			1,
			false,        // don't load SA samp in reverse index
			true,         // yes, need ftab in reverse index
			false,        // don't load rstarts in reverse index
			!noRefNames,  // load names?
			startVerbose);
	}
	// Start the metrics thread
	//MetricsThread mett(cerr, metrics, (time_t)metricsIval);
	//if(metricsIval > 0) mett.run();
	{
		Timer _t(cerr, "Multiseed full-index search: ", timing);
#ifdef BOWTIE_PTHREADS
		for(int i = 0; i < nthreads-1; i++) {
			// Thread IDs start at 1
			tids[i] = i+1;
			createThread(&threads[i], multiseedSearchWorker, (void*)&tids[i]);
		}
#endif
		int tmp = 0;
		multiseedSearchWorker((void*)&tmp);
#ifdef BOWTIE_PTHREADS
		for(int i = 0; i < nthreads-1; i++) joinThread(threads[i]);
#endif
	}
	if(metricsIval > 0 && (metricsOfb != NULL || metricsStderr)) {
		metrics.reportInterval(metricsOfb, metricsStderr);
	}
	//if(metricsIval > 0) { mett.kill(); mett.join(); }
}

static string argstr;

template<typename TStr>
static void driver(
	const char * type,
	const string& ebwtFileBase,
	const string& query,
	const EList<string>& queries,
	const EList<string>& qualities,
	const string& outfile)
{
	if(gVerbose || startVerbose)  {
		cerr << "Entered driver(): "; logTime(cerr, true);
	}
	// Vector of the reference sequences; used for sanity-checking
	EList<SString<char> > names, os;
	EList<size_t> nameLens, seqLens;
	// Read reference sequences from the command-line or from a FASTA file
	if(!origString.empty()) {
		// Read fasta file(s)
		EList<string> origFiles;
		tokenize(origString, ",", origFiles);
		parseFastas(origFiles, names, nameLens, os, seqLens);
	}
	PatternParams pp(
		format,        // file format
		fileParallel,  // true -> wrap files with separate PairedPatternSources
		seed,          // pseudo-random seed
		useSpinlock,   // use spin locks instead of pthreads
		patDumpfile,   // file name of file to dump to
		solexaQuals,   // true -> qualities are on solexa64 scale
		phred64Quals,  // true -> qualities are on phred64 scale
		integerQuals,  // true -> qualities are space-separated numbers
		fuzzy,         // true -> try to parse fuzzy fastq
		fastaContLen,  // length of sampled reads for FastaContinuous...
		fastaContFreq, // frequency of sampled reads for FastaContinuous...
		skipReads      // skip the first 'skip' patterns
	);
	if(gVerbose || startVerbose) {
		cerr << "Creating PatternSource: "; logTime(cerr, true);
	}
	PairedPatternSource *patsrc = PairedPatternSource::setupPatternSources(
		queries,     // singles, from argv
		mates1,      // mate1's, from -1 arg
		mates2,      // mate2's, from -2 arg
		mates12,     // both mates on each line, from --12 arg
		qualities,   // qualities associated with singles
		qualities1,  // qualities associated with m1
		qualities2,  // qualities associated with m2
		pp,          // read read-in parameters
		gVerbose || startVerbose); // be talkative
	// Open hit output file
	if(gVerbose || startVerbose) {
		cerr << "Opening hit output file: "; logTime(cerr, true);
	}
	OutFileBuf *fout;
	if(!outfile.empty()) {
		fout = new OutFileBuf(outfile.c_str(), false);
	} else {
		fout = new OutFileBuf();
	}
	// Initialize Ebwt object and read in header
	if(gVerbose || startVerbose) {
		cerr << "About to initialize fw Ebwt: "; logTime(cerr, true);
	}
	adjIdxBase = adjustEbwtBase(argv0, ebwtFileBase, gVerbose);
	Ebwt ebwt(
		adjIdxBase,
	    gColor,   // index is colorspace
		-1,       // fw index
	    true,     // index is for the forward direction
	    /* overriding: */ offRate,
		0, // amount to add to index offrate or <= 0 to do nothing
	    useMm,    // whether to use memory-mapped files
	    useShmem, // whether to use shared memory
	    mmSweep,  // sweep memory-mapped files
	    !noRefNames, // load names?
		true,        // load SA sample?
		true,        // load ftab?
		true,        // load rstarts?
	    gVerbose, // whether to be talkative
	    startVerbose, // talkative during initialization
	    false /*passMemExc*/,
	    sanityCheck);
	Ebwt* ebwtBw = NULL;
	// We need the mirror index if mismatches are allowed
	if(multiseedMms > 0) {
		if(gVerbose || startVerbose) {
			cerr << "About to initialize rev Ebwt: "; logTime(cerr, true);
		}
		ebwtBw = new Ebwt(
			adjIdxBase + ".rev",
			gColor,  // index is colorspace
			1,       // TODO: maybe not
		    false, // index is for the reverse direction
		    /* overriding: */ offRate,
			0, // amount to add to index offrate or <= 0 to do nothing
		    useMm,    // whether to use memory-mapped files
		    useShmem, // whether to use shared memory
		    mmSweep,  // sweep memory-mapped files
		    !noRefNames, // load names?
			true,        // load SA sample?
			true,        // load ftab?
			true,        // load rstarts?
		    gVerbose,    // whether to be talkative
		    startVerbose, // talkative during initialization
		    false /*passMemExc*/,
		    sanityCheck);
	}
	if(sanityCheck && !os.empty()) {
		// Sanity check number of patterns and pattern lengths in Ebwt
		// against original strings
		assert_eq(os.size(), ebwt.nPat());
		for(size_t i = 0; i < os.size(); i++) {
			assert_eq(os[i].length(), ebwt.plen()[i] + (gColor ? 1 : 0));
		}
	}
	// Sanity-check the restored version of the Ebwt
	if(sanityCheck && !os.empty()) {
		ebwt.loadIntoMemory(
			gColor ? 1 : 0,
			-1, // fw index
			true, // load SA sample
			true, // load ftab
			true, // load rstarts
			!noRefNames,
			startVerbose);
		ebwt.checkOrigs(os, gColor, false);
		ebwt.evictFromMemory();
	}
	{
		Timer _t(cerr, "Time searching: ", timing);
		// Set up penalities
		Scoring sc(
			bonusMatch,     // constant reward for match
			penMmcType,     // how to penalize mismatches
			penMmc,         // constant if mm pelanty is a constant
			penSnp,         // pena for nuc mm in decoded colorspace alns
			scoreMin,       // min score as function of read len
			scoreFloor,     // floor score as function of read len
			nCeil,          // max # Ns as function of read len
			penNType,       // how to penalize Ns in the read
			penN,           // constant if N pelanty is a constant
			penNCatPair,    // whether to concat mates before N filtering
			penRdGapConst,  // constant coeff for read gap cost
			penRfGapConst,  // constant coeff for ref gap cost
			penRdGapLinear, // linear coeff for read gap cost
			penRfGapLinear, // linear coeff for ref gap cost
			gGapBarrier,    // # rows at top/bot only entered diagonally
			gRowLow,        // min row idx to backtrace from; -1 = no limit
			gRowFirst);     // sort results first by row then by score?
		EList<size_t> reflens;
		for(size_t i = 0; i < ebwt.nPat(); i++) {
			reflens.push_back(ebwt.plen()[i]);
		}
		EList<string> refnames;
		readEbwtRefnames(adjIdxBase, refnames);
		SamConfig samc(
			refnames,               // reference sequence names
			reflens,                // reference sequence lengths
			samTruncQname,          // whether to truncate QNAME to 255 chars
			samOmitSecSeqQual,      // omit SEQ/QUAL for 2ndary alignments?
			string("bowtie2"),      // program id
			string("bowtie2"),      // program name
			string(BOWTIE_VERSION), // program version
			argstr,                 // command-line
			rgs_optflag,            // read-group string
			sam_print_as,
			sam_print_xs,
			sam_print_xn,
			sam_print_cs,
			sam_print_cq,
			sam_print_x0,
			sam_print_x1,
			sam_print_xm,
			sam_print_xo,
			sam_print_xg,
			sam_print_nm,
			sam_print_md,
			sam_print_yf,
			sam_print_yi,
			sam_print_ym,
			sam_print_yp,
			sam_print_yt,
			sam_print_ys);
		// Set up hit sink; if sanityCheck && !os.empty() is true,
		// then instruct the sink to "retain" hits in a vector in
		// memory so that we can easily sanity check them later on
		AlnSink *mssink = NULL;
		switch(outType) {
			case OUTPUT_FULL: {
				mssink = new AlnSinkVerbose(
					fout,         // initial output stream
					suppressOuts, // suppress alignment columns
					false,        // delete output stream objects upon destruction
					refnames,     // reference names
					gQuiet,       // don't print alignment summary at end   
					0,            // add this to 0-based offsets before printing
					gColorSeq,    // colorspace: print color seq instead of decoded nucs
					gColorQual,   // colorspace: print color quals instead of decoded quals
					gColorExEnds, // whether to exclude end positions from decoded colorspace alignments
					printPlaceholders, // print maxs/unals
					printFlags,   // print alignment flags
					false,        // print penalty in extra column
					printParams,  // print alignment parameters in extra column
					fullRef,      // print entire reference name including whitespace
					partitionSz); // size of partition, so we can check for straddling alignments
				break;
			}
			case OUTPUT_SAM: {
				mssink = new AlnSinkSam(
					fout,         // initial output stream
					samc,         // settings & routines for SAM output
					false,        // delete output stream objects upon destruction
					refnames,     // reference names
					gQuiet,       // don't print alignment summary at end
					gColorExEnds);// exclude ends from decoded colorspace alns?
				if(!samNoHead) {
					bool printHd = true, printSq = true;
					samc.printHeader(*fout, rgid, rgs, printHd, !samNoSQ, printSq);
				}
				break;
			}
			default:
				cerr << "Invalid output type: " << outType << endl;
				throw 1;
		}
		if(gVerbose || startVerbose) {
			cerr << "Dispatching to search driver: "; logTime(cerr, true);
		}
		// Set up global constraint
		Constraint gc = Constraint::penaltyFuncBased(scoreMin);
		// Set up seeds
		EList<Seed> seeds;
		Seed::mmSeeds(
			multiseedMms,    // max # mms allowed in a multiseed seed
			multiseedLen,    // length of a multiseed seed (scales down if read is shorter)
			seeds,           // seeds
			gc);             // global constraint
		// Set up listeners for alignment progress
		EList<SeedHitSink*> seedHitSink;
		EList<SeedCounterSink*> seedCounterSink;
		EList<SeedActionSink*> seedActionSink;
		ofstream *hitsOf = NULL, *cntsOf = NULL, *actionsOf = NULL;
		if(!saHitsFn.empty()) {
			hitsOf = new ofstream(saHitsFn.c_str());
			if(!hitsOf->is_open()) {
				cerr << "Error: Unable to open seed hit dump file " << saHitsFn << endl;
				throw 1;
			}
			seedHitSink.push_back(new StreamTabSeedHitSink(*hitsOf));
		}
		if(!saCountersFn.empty()) {
			cntsOf = new ofstream(saCountersFn.c_str());
			if(!cntsOf->is_open()) {
				cerr << "Error: Unable to open seed counter dump file " << saCountersFn << endl;
				throw 1;
			}
			seedCounterSink.push_back(new StreamTabSeedCounterSink(*cntsOf));
		}
		if(!saActionsFn.empty()) {
			actionsOf = new ofstream(saActionsFn.c_str());
			if(!actionsOf->is_open()) {
				cerr << "Error: Unable to open seed action dump file " << saActionsFn << endl;
				throw 1;
			}
			seedActionSink.push_back(new StreamTabSeedActionSink(*actionsOf));
		}
		OutFileBuf *metricsOfb = NULL;
		if(!metricsFile.empty() && metricsIval > 0) {
			metricsOfb = new OutFileBuf(metricsFile);
		}
		// Do the search for all input reads
		assert(patsrc != NULL);
		assert(mssink != NULL);
		multiseedSearch(
			sc,      // scoring scheme
			seeds,   // seeds
			*patsrc, // pattern source
			*mssink, // hit sink
			ebwt,    // BWT
			*ebwtBw, // BWT'
			seedHitSink,
			seedCounterSink,
			seedActionSink,
			metricsOfb);
		for(size_t i = 0; i < seedHitSink.size();     i++) delete seedHitSink[i];
		for(size_t i = 0; i < seedCounterSink.size(); i++) delete seedCounterSink[i];
		for(size_t i = 0; i < seedActionSink.size();  i++) delete seedActionSink[i];
		if(hitsOf != NULL) delete hitsOf;
		if(cntsOf != NULL) delete cntsOf;
		if(actionsOf != NULL) delete actionsOf;
		if(metricsOfb != NULL) delete metricsOfb;
		// Evict any loaded indexes from memory
		if(ebwt.isInMemory()) {
			ebwt.evictFromMemory();
		}
		if(ebwtBw != NULL) {
			delete ebwtBw;
		}
		if(!gQuiet && !seedSummaryOnly) {
			size_t repThresh = mhits;
			if(repThresh == 0) {
				repThresh = std::numeric_limits<size_t>::max();
			}
			mssink->finish(
				repThresh,
				gReportDiscordant,
				gReportMixed,
				hadoopOut);
		}
		delete patsrc;
		delete mssink;
		if(fout != NULL) delete fout;
	}
}

// C++ name mangling is disabled for the bowtie() function to make it
// easier to use Bowtie as a library.
extern "C" {

/**
 * Main bowtie entry function.  Parses argc/argv style command-line
 * options, sets global configuration variables, and calls the driver()
 * function.
 */
int bowtie(int argc, const char **argv) {
	try {
		// Reset all global state, including getopt state
		opterr = optind = 1;
		resetOptions();
		for(int i = 0; i < argc; i++) {
			argstr += argv[i];
			if(i < argc-1) argstr += " ";
		}
		string ebwtFile;  // read serialized Ebwt from this file
		string query;   // read query string(s) from this file
		EList<string> queries(0);
		string outfile; // write query results to this file
		if(startVerbose) { cerr << "Entered main(): "; logTime(cerr, true); }
		parseOptions(argc, argv);
		argv0 = argv[0];
		if(showVersion) {
			cout << argv0 << " version " << BOWTIE_VERSION << endl;
			if(sizeof(void*) == 4) {
				cout << "32-bit" << endl;
			} else if(sizeof(void*) == 8) {
				cout << "64-bit" << endl;
			} else {
				cout << "Neither 32- nor 64-bit: sizeof(void*) = " << sizeof(void*) << endl;
			}
			cout << "Built on " << BUILD_HOST << endl;
			cout << BUILD_TIME << endl;
			cout << "Compiler: " << COMPILER_VERSION << endl;
			cout << "Options: " << COMPILER_OPTIONS << endl;
			cout << "Sizeof {int, long, long long, void*, size_t, off_t}: {"
				 << sizeof(int)
				 << ", " << sizeof(long) << ", " << sizeof(long long)
				 << ", " << sizeof(void *) << ", " << sizeof(size_t)
				 << ", " << sizeof(off_t) << "}" << endl;
			return 0;
		}
		{
			Timer _t(cerr, "Overall time: ", timing);
			if(startVerbose) {
				cerr << "Parsing index and read arguments: "; logTime(cerr, true);
			}

			// Get index basename
			if(optind >= argc) {
				cerr << "No index, query, or output file specified!" << endl;
				printUsage(cerr);
				return 1;
			}
			ebwtFile = argv[optind++];

			// Get query filename
			if(optind >= argc) {
				if(mates1.size() > 0 || mates12.size() > 0) {
					query = "";
				} else {
					cerr << "No query or output file specified!" << endl;
					printUsage(cerr);
					return 1;
				}
			} else if (mates1.size() == 0 && mates12.size() == 0) {
				query = argv[optind++];
				// Tokenize the list of query files
				tokenize(query, ",", queries);
				if(queries.size() < 1) {
					cerr << "Tokenized query file list was empty!" << endl;
					printUsage(cerr);
					return 1;
				}
			}

			// Get output filename
			if(optind < argc) {
				outfile = argv[optind++];
			}

			// Extra parametesr?
			if(optind < argc) {
				cerr << "Extra parameter(s) specified: ";
				for(int i = optind; i < argc; i++) {
					cerr << "\"" << argv[i] << "\"";
					if(i < argc-1) cerr << ", ";
				}
				cerr << endl;
				if(mates1.size() > 0) {
					cerr << "Note that if <mates> files are specified using -1/-2, a <singles> file cannot" << endl
						 << "also be specified.  Please run bowtie separately for mates and singles." << endl;
				}
				throw 1;
			}

			// Optionally summarize
			if(gVerbose) {
				cout << "Input bt2 file: \"" << ebwtFile << "\"" << endl;
				cout << "Query inputs (DNA, " << file_format_names[format] << "):" << endl;
				for(size_t i = 0; i < queries.size(); i++) {
					cout << "  " << queries[i] << endl;
				}
				cout << "Quality inputs:" << endl;
				for(size_t i = 0; i < qualities.size(); i++) {
					cout << "  " << qualities[i] << endl;
				}
				cout << "Output file: \"" << outfile << "\"" << endl;
				cout << "Local endianness: " << (currentlyBigEndian()? "big":"little") << endl;
				cout << "Sanity checking: " << (sanityCheck? "enabled":"disabled") << endl;
			#ifdef NDEBUG
				cout << "Assertions: disabled" << endl;
			#else
				cout << "Assertions: enabled" << endl;
			#endif
			}
			if(ipause) {
				cout << "Press key to continue..." << endl;
				getchar();
			}
			driver<SString<char> >("DNA", ebwtFile, query, queries, qualities, outfile);
		}
		return 0;
	} catch(exception& e) {
		cerr << "Command: ";
		for(int i = 0; i < argc; i++) cerr << argv[i] << " ";
		cerr << endl;
		return 1;
	} catch(int e) {
		if(e != 0) {
			cerr << "Command: ";
			for(int i = 0; i < argc; i++) cerr << argv[i] << " ";
			cerr << endl;
		}
		return e;
	}
} // bowtie()
} // extern "C"
