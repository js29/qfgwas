/*
 * SNPs.cpp
 *
 *  Created on: Jan 14, 2013
 *      Author: pickrell
 */

#include "SNPs.h"
#include <algorithm>
using namespace std;

double FIXED_B1_VAL = 0.0;

inline string strtoupper(string str)
{
	std::transform(str.begin(), str.end(), str.begin(), ::toupper);
	return str;
}

SNPs::SNPs(){

}

SNPs::SNPs(Fgwas_params *p){
	params = p;
	params->print_stdout();

	//read distance models
	for (vector<string>::iterator it = params->distmodels.begin(); it != params->distmodels.end(); it++) dmodels.push_back( read_dmodel(*it));

	//read input file
	if (params->zformat) {
		load_snps_z(params->infile, params->V, params->wannot, params->quantannot, params->dannot, params->segannot);
	} else {
		cerr << "ERROR: need z-score format for now\n";
		exit(1);
	}

	//make segments
	if (params->finemap) make_segments_finemap();
	else{
		make_chrsegments();
		if (params->bedseg) make_segments(params->segment_bedfile);
		else make_segments(params->K);
	}
	//double-check input quality
	check_input();

	//initialize
	snppri.clear();
	segpi = 0.001;
	init_segpriors();
    phi = (1+sqrt(5))/2;
    resphi = 2-phi;
	for (int i = 0; i < d.size(); i++){
		snppri.push_back(1.0);
		snppost.push_back(0.0);
	}
	nannot = annotnames.size();
	nsegannot = segannotnames.size();
	for (int i = 0; i < nannot; i++)	lambdas.push_back(0);
	for (int i = 0; i < nsegannot; i++) seglambdas.push_back(0);
	
	quantModelParamNum = 3;
	if (FIXED_B1_VAL > 0) {
		quantModelParamNum = 2;
	}
	for (int i = 0; i < quantannotnames.size(); i++) {
		quantparams.push_back(QuantParams(0, 0, FIXED_B1_VAL));
	}
	set_priors();
}

void SNPs::check_input(){

	double meansize = 0.0;
	double meannsnp = 0.0;
	int minnsnp = 1000000000;
	int maxsnp = 0;
	int nseg = segments.size();
	for (vector<pair<int, int> >::iterator it = segments.begin(); it != segments.end(); it++){

		int st = it->first;
		int sp = it->second;
		int l = sp-st;
		if (l < minnsnp) minnsnp = l;
		if (l > maxsnp) maxsnp = l;
		meannsnp += (double) l / (double) nseg;
		//cout << st <<  " "<< sp << "\n";
		int toadd = d[sp-1].pos- d[st].pos;


		meansize += ((double) toadd/ 1000000.0) / (double) nseg;

		int prevpos = d[st].pos;
		string prevchr = d[st].chr;
		for (int i= st+1; i < sp; i++){
			string testchr = d[i].chr;
			int testpos = d[i].pos;
			if (testchr == prevchr and prevpos >= testpos){  //test that each segment is only a single chromosome, is ordered
				cerr<< "ERROR: SNPs out of order\nChromosome "<<testchr << ". Position "<< prevpos << " seen before "<< testpos<< "\n";
				exit(1);
			}
			prevpos = testpos;
			prevchr = testchr;
		}
	}

	cout << "Number of segments: "<< segments.size()<< "\nMean segment size: "<< meansize<< " Mb ("<< meannsnp << " SNPs)\n";
	cout << "Minimum number of SNPs/segment: "<< minnsnp << "\n";
	cout << "Maximum number of SNPs/segment: "<< maxsnp << "\n";
	if (meansize > 10.0){
		cout << "\n****\n**** WARNING: mean segment size is over 10Mb, this often causes convergence problems (in human data). Consider reducing window size (using -k).\n****\n\n"; cout.flush();
	}
}
vector<pair<int, int> > SNPs::read_dmodel(string infile){
	vector<pair<int, int> > toreturn;
	ifstream in(infile.c_str());
	vector<string> line;
	struct stat stFileInfo;
	int intStat;
	string st, buf;

	intStat = stat(infile.c_str(), &stFileInfo);
	if (intStat !=0){
		std::cerr<< "ERROR: cannot open file " << infile << "\n";
		exit(1);
	}
    while(getline(in, st)){
    	buf.clear();
    	stringstream ss(st);
    	line.clear();
    	while (ss>> buf){
    		line.push_back(buf);
    	}
    	int start = atoi(line[0].c_str());
    	int stop = atoi(line[1].c_str());
    	if (stop < start){
    		cerr <<"ERROR: in distance model "<< infile<< " , " << start << " is after "<< stop << "\n";
    		exit(1);
    	}
    	toreturn.push_back(make_pair(start, stop));
    }
	return toreturn;
}

void SNPs::init_segpriors(){
	segannot.clear();
	segpriors.clear();
	segannotnames.clear();
	vector<double> segmeans;
	vector<double> means2sort;
	for (vector<pair<int, int> >::iterator it = segments.begin(); it != segments.end(); it++){
		segpriors.push_back(segpi);
		if (params->segannot.size() > 0){
			double segmean = 0.0;
			int total = 0;
			for (int i = it->first; i < it->second; i++){
				segmean += d[i].dens;
				total ++;
			}
			segmeans.push_back( segmean / (double) total);
			means2sort.push_back( segmean / (double) total);
		}
	}
	if (params->segannot.size() < 1) return;

	segannotnames.push_back(params->segannot[0] +"_lo");
	segannotnames.push_back(params->segannot[0] +"_hi");
	sort(means2sort.begin(), means2sort.end());
	double locutoff = means2sort[floor( (double) means2sort.size()* params->loquant ) ];
	double hicutoff = means2sort[floor( (double) means2sort.size()* params->hiquant ) ];
	for (int i = 0; i < segments.size(); i++){
		vector<bool> annots;
		double m = segmeans[i];
		if (m <= locutoff) annots.push_back(true);
		else annots.push_back(false);
		if (m >= hicutoff) annots.push_back(true);
		else annots.push_back(false);
		segannot.push_back(annots);
	}
}

void SNPs::load_snps_z(string infile, vector<double> prior, vector<string> annot, vector<string> qannot, vector<string> dannot, vector<string> segannot){
	igzstream in(infile.c_str()); //only gzipped files
	vector<string> line;
	struct stat stFileInfo;
	int intStat;
	string st, buf;

	intStat = stat(infile.c_str(), &stFileInfo);
	if (intStat !=0){
		std::cerr<< "ERROR: cannot open file " << infile << "\n";
		exit(1);
	}

	// read header
	getline(in, st);
	buf.clear();
	stringstream ss(st);
	line.clear();
	while (ss>> buf){
		line.push_back(buf);
	}

	//make a map of header to index
	map<string, int> header_index;
	for (int i = 0; i < line.size(); i++){
		header_index[line[i]] = i;
	}
	// get the indices of the annotations
	vector<int> annot_index;
   	for (vector<string>::iterator it = annot.begin(); it != annot.end(); it++){
   		int i = 0;
   		bool found = false;
   		while (i < line.size() and !found){
   			if (line[i] == *it) {
   				annot_index.push_back(i);
   				found = true;
   			}
   			i++;
   		}
   		if (!found){
   			cerr << "ERROR: cannot find annotation "<< *it << "\n";
   			exit(1);
   		}
    	annotnames.push_back(*it);
   	}
	// get the indices of quantitative annotations
	vector<int> qannot_index;
   	for (vector<string>::iterator it = qannot.begin(); it != qannot.end(); it++){
   		int i = 0;
   		bool found = false;
   		while (i < line.size() and !found){
   			if (line[i] == *it) {
   				qannot_index.push_back(i);
   				found = true;
   			}
   			i++;
   		}
   		if (!found){
   			cerr << "ERROR: cannot find annotation "<< *it << "\n";
   			exit(1);
   		}
    	quantannotnames.push_back(*it); // @@@
   	}

   	// get indices of distance annotations
   	vector<int> dannot_index;
   	for (int j = 0; j < dannot.size(); j++){
   		string jname = dannot[j];
   		int i = 0;
   		bool found = false;
   		while (i < line.size() and !found){
   			if (line[i] == jname) {
   				dannot_index.push_back(i);
   				found = true;
   			}
   			i++;
   		}
   		if (!found){
   			cerr << "ERROR: cannot find distance annotation "<< jname << "\n";
   			exit(1);
   		}
   		append_dannotnames(jname, dmodels[j]);
   	}
   	// get indices for segannot
   	int segannotindex;
   	if (segannot.size() > 0){
   		if (header_index.find(segannot[0]) == header_index.end()){
   			cerr << "ERROR: cannot find segment annotation "<< segannot[0] << "\n";
   			exit(1);
   		}
   		segannotindex = header_index[segannot[0]];
   	}
   	// get indices for the rs, maf, chr, pos, N, Ncase, Ncontrol,
   	int rsindex, mafindex, chrindex, zindex, posindex, Nindex, Ncaseindex, Ncontrolindex, segnumberindex, condindex, bfindex;
   	bool override_v = false;
   	bool override_z = false;
   	int seindex;
   	if (header_index.find("SEGNUMBER") != header_index.end() && params->finemap == false){
   		cout << "WARNING: detected SEGNUMBER in header, but no -fine flag. Are you sure you're not using the fine-mapping format?\n";
   	}
   	if (header_index.find("SE") != header_index.end()){
   		cout << "WARNING: detected SE in header, will override F and N\n";
   		seindex = header_index["SE"];
   		override_v = true;
   	}
  	if (header_index.find("LNBF") != header_index.end()){
   		cout << "WARNING: detected LNBF in header, will override Z, F and N\n";
   		bfindex = header_index["LNBF"];
   		override_z = true;
   	}
   	if (header_index.find("SNPID") == header_index.end()){
   		cerr << "ERROR: cannot find SNPID in header\n";
   		exit(1);
   	}
   	else rsindex = header_index["SNPID"];

  	if (header_index.find("F") == header_index.end()){
   		cerr << "ERROR: cannot find F in header\n";
   		exit(1);
   	}
   	else mafindex = header_index["F"];

 	if (header_index.find("CHR") == header_index.end()){
   		cerr << "ERROR: cannot find CHR in header\n";
   		exit(1);
   	}
   	else chrindex = header_index["CHR"];

 	if (header_index.find("POS") == header_index.end()){
   		cerr << "ERROR: cannot find POS in header\n";
   		exit(1);
   	}
   	else posindex = header_index["POS"];

	if (header_index.find("Z") == header_index.end()){
   		cerr << "ERROR: cannot find Z in header\n";
   		exit(1);
   	}
   	else zindex = header_index["Z"];

	if (header_index.find("N") == header_index.end() && !params->cc){
   		cerr << "ERROR: cannot find N in header\n";
   		exit(1);
   	}
   	else Nindex = header_index["N"];

	if (header_index.find("NCASE") == header_index.end() && params->cc){
   		cerr << "ERROR: cannot find NCASE in header\n";
   		exit(1);
   	}
   	else Ncaseindex = header_index["NCASE"];

	if (header_index.find("NCONTROL") == header_index.end() && params->cc){
   		cerr << "ERROR: cannot find NCONTROL in header\n";
   		exit(1);
   	}
   	else Ncontrolindex = header_index["NCONTROL"];

	if (header_index.find("SEGNUMBER") == header_index.end() && params->finemap){
   		cerr << "ERROR: cannot find SEGNUMBER in header\n";
   		exit(1);
   	}
   	else segnumberindex = header_index["SEGNUMBER"];

	if (params->cond && header_index.find(params->testcond_annot) == header_index.end()){
		cerr << "ERROR: cannot find annotation "<< params->testcond_annot << "\n";
		exit(1);
	}
	else if (params->cond) condindex = header_index[params->testcond_annot];
	string oldchr = "NA";
    while(getline(in, st)){
    	buf.clear();
    	stringstream ss(st);
    	line.clear();
    	while (ss>> buf){
    		line.push_back(buf);
    	}
    	string rs = line[rsindex];

    	double alfreq = atof(line[mafindex].c_str());
    	if (alfreq < 1e-8 and !override_z and !override_v) continue;
    	double z = atof(line[zindex].c_str());

    	//quantitative trait
    	if (!params->cc){
    		int N = atoi(line[Nindex].c_str());
    		string chr = line[chrindex];
    		if (chr != oldchr) {
    			//cout << "Reading "<< chr << "\n"; cout.flush();
    			oldchr = chr;
    		}
    		if (params->dropchr and chr == params->chrtodrop) continue;
    		int pos = atoi(line[posindex].c_str());
    		vector<bool> an;
    		vector<int> dists;
    		for (vector<int>::iterator it = annot_index.begin(); it != annot_index.end(); it++){
    			if (line[*it] == "1") an.push_back(true);
    			else if (line[*it] == "0") an.push_back(false);
    			else{
    				cerr << "ERROR: only 0 and 1 allowed for annotations, found "<< line[*it] <<"\n";
    				exit(1);
    			}
    		}
    		for (vector<int>::iterator it = dannot_index.begin(); it != dannot_index.end(); it++){
    			dists.push_back( atoi(line[*it].c_str()));
    		}

    		SNP s(rs, chr , pos, N, alfreq, z, prior, an, dists, dmodels);
    		
    		for (vector<int>::iterator it = qannot_index.begin(); it != qannot_index.end(); it++){
				if (strtoupper(line[*it]).compare("NA") == 0) {
					s.qannotDefined.push_back(false);
					s.qannot.push_back(0);
				} else {
					s.qannotDefined.push_back(true);
					s.qannot.push_back(atof(line[*it].c_str()));
				}
    		}
    		if (params->finemap){
    			check_string2digit(line[segnumberindex]);
    			int snumber = atoi(line[segnumberindex].c_str());
    			s.chunknumber = snumber;
    		}
    		if (segannot.size() > 0) s.dens = atof(line[segannotindex].c_str());

    		//if there's SE in the header
    		if (override_v){
    			float se = atof(line[seindex].c_str());
    			s.V = se*se;
    			s.BF = s.calc_logBF();
    		}
    		if (override_z){
    			float lnBF = atof(line[bfindex].c_str());
    			s.BF = lnBF;
    		}
    		if (params->cond){
    			if (line[condindex] == "1") s.condannot =true;
    			else if (line[condindex] == "0") s.condannot = false;
    			else{
    				cerr << "ERROR: only 0 and 1 allowed for annotations, found "<< line[condindex] <<"\n";
    				exit(1);
    			}
    		}
    		d.push_back(s);
    	}

    	//case-control study
    	else{
      		int Ncase = atoi(line[Ncaseindex].c_str());
      		int Ncontrol = atoi(line[Ncontrolindex].c_str());
      		string chr = line[chrindex];
      		if (params->dropchr and chr == params->chrtodrop) continue;
      		int pos = atoi(line[posindex].c_str());
      		vector<bool> an;
      		vector<int> dists;
      		for (vector<int>::iterator it = annot_index.begin(); it != annot_index.end(); it++){
    			if (line[*it] == "1") an.push_back(true);
    			else if (line[*it] == "0") an.push_back(false);
    			else{
    				cerr << "ERROR: only 0 and 1 allowed for annotations, found "<< line[*it] <<"\n";
    				exit(1);
    			}

        	}
      		for (vector<int>::iterator it = dannot_index.begin(); it != dannot_index.end(); it++){
      			dists.push_back( atoi(line[*it].c_str()));
        	}

      		SNP s(rs, chr , pos, Ncase, Ncontrol, alfreq, z, prior, an, dists, dmodels);
    		if (params->finemap){
    			check_string2digit(line[segnumberindex]);
    			int snumber = atoi(line[segnumberindex].c_str());
    			s.chunknumber = snumber;
    		}
      		if (segannot.size() > 0) s.dens = atof(line[segannotindex].c_str());

      		//if there's SE in the header
       		if (override_v){
       			float se = atof(line[seindex].c_str());
       			s.V = se*se;
       			s.BF = s.calc_logBF();
       		}
      		if (override_z){
        			float lnBF = atof(line[bfindex].c_str());
        			s.BF = lnBF;
      		}
      		if (params->cond){
      			if (line[condindex] == "1") s.condannot =true;
      			else if (line[condindex] == "0") s.condannot = false;
      			else{
      				cerr << "ERROR: only 0 and 1 allowed for annotations, found "<< line[condindex] <<"\n";
      				exit(1);
      			}
      		}
      		d.push_back(s);

    	}
    }
    cout << "Read "<< d.size() << " variants\n";
}

void SNPs::check_string2digit(string s){
	for (int i = 0; i < s.size(); i++){
		if (!isdigit(s.at(i))){
			cerr << "ERROR: "<< s <<" is used in SEGNUMBER but is not a number\n";
			exit(1);
		}
	}
}
void SNPs::append_dannotnames(string name, vector<pair<int, int> > model){
	for (vector<pair<int, int> >::iterator it = model.begin(); it != model.end(); it++){
		stringstream ss;
		ss << name << "_" << it->first << "_"<< it->second;
		annotnames.push_back(ss.str());
	}
}

vector<pair< pair<int, int>, pair<double, double> > > SNPs::get_cis(){
	vector<pair<pair<int, int>, pair<double, double> > > toreturn;
	double startsegpi = segpi;
	vector<double> startlambdas;
	vector<double> startquantlambdas;
	vector<double> startseglambdas;
	for (vector<double>::iterator it = lambdas.begin(); it != lambdas.end(); it++) startlambdas.push_back(*it);
	for (vector<QuantParams>::iterator it = quantparams.begin(); it != quantparams.end(); it++) startquantlambdas.push_back(it->lambda);
	for (vector<double>::iterator it = seglambdas.begin(); it != seglambdas.end(); it++) startseglambdas.push_back(*it);
	if (!params->finemap) {
		toreturn.push_back(get_cis_segpi());
		segpi = startsegpi;
		set_priors();
		for (int i = 0; i < seglambdas.size(); i++){
			toreturn.push_back(get_cis_param(&seglambdas[i]));
			seglambdas[i] = startseglambdas[i];
			set_priors();
		}
	}
	segpi = startsegpi;
	for (int i = 0; i < lambdas.size(); i++){
		toreturn.push_back(get_cis_param(&lambdas[i]));
		lambdas[i] = startlambdas[i];
		set_priors();
	}
	for (int i = 0; i < quantparams.size(); i++){
		toreturn.push_back(get_cis_param(&quantparams[i].lambda));
		quantparams[i].lambda = startquantlambdas[i];
		set_priors();
	}
	return toreturn;
}

pair< pair<int, int>, pair<double, double> > SNPs::get_cis_condlambda(){
	double startlk = llk();
	double thold = startlk - 2;
	cout <<  startlk << " "<< thold << "\n";
	double min = -20.0;
	double max = 20.0;
	double test = condlambda;
	if (test > max) max = test+20.0;
	if (test < min) min = test-20.0;
	if (max < 0) max = 20.0;
	if (min > 0) min = -20.0;
	double start = (test+max)/2;
	double tau = 0.001;
	int convhi = golden_section_condlambda_ci(test, start, max, tau, thold);
	double hi = condlambda;
	cout << hi << " "<< llk() << " hi\n";
	start = (test+min)/2;
	int convlo = golden_section_condlambda_ci(min, start, test, tau, thold);
	double lo = condlambda;
	cout << lo << " "<< llk() << " lo\n";
	pair<int, int> conv = make_pair(convlo, convhi);
	pair<double, double> ci = make_pair(lo, hi);
	return make_pair(conv, ci);
}

pair< pair<int, int>, pair<double, double> > SNPs::get_cis_segpi(){
	double startsegpi = segpi;
	double startlk = llk();
	double thold = startlk - 2;
	//cout <<  startlk << " "<< thold << "\n";
	double test =  log(startsegpi) - log(1-startsegpi);

	double min = -10.0;
	if (min > test) min = test - 10;
	double max = 10.0;
	if (max < test) max = test+ 10;
	int convhi, convlo;
	double hi, lo;
	double tau = 0.001;
	// see if value at max is less than thold
	segpi = 1.0  / ( 1.0 + exp(-max));
	set_priors();
	double maxllk = llk();

	//if yes, do optimization
	if (maxllk < thold){
		int nit = 0;
		double start = (test+max)/2;

		convhi = golden_section_segpi_ci(test, start, max, tau, thold,  &nit);
		hi = segpi;
	}
	else{
		hi =1.0  / ( 1.0 + exp(-max));
		convhi = 2;
	}
	cout << hi << " "<< llk() << " hi\n";

	// same for min
	segpi = 1.0  / ( 1.0 + exp(-min));
	set_priors();
	double minllk = llk();

	if (minllk < thold){
		double start = (test+min)/2;
		int nit = 0;
		convlo = golden_section_segpi_ci(min, start, test, tau, thold, &nit);
		lo = segpi;
	}
	else{
		lo = 1.0  / ( 1.0 + exp(-min));
		convlo = 2;
	}
	cout << lo << " "<< llk() << " lo\n";
	pair<int, int> conv = make_pair(convlo, convhi);
	pair<double, double> ci = make_pair(lo, hi);
	return make_pair(conv, ci);
}

pair< pair<int, int>, pair<double, double> > SNPs::get_cis_param(double* pParam) {
	double startlk = llk();
	double thold = startlk - 2;
	cout <<  startlk << " "<< thold << "\n";
	double min = -20.0;
	double max = 20.0;
	double test = *pParam;
	if (test > max) max = test+20.0;
	if (test < min) min = test-20.0;
	if (max < 0) max = 20.0;
	if (min > 0) min = -20.0;
	double start = (test+max)/2;
	double tau = 0.001;
	double hi, lo;
	int convhi, convlo;


	//test if maximum is less than thold
	*pParam = max;
	set_priors();
	double maxllk = llk();
	if (maxllk < thold) {
		convhi = golden_section_ci(test, start, max, tau, thold, pParam);
		hi = *pParam;
	} else {
		convhi = 2;
		hi = max;
	}
	cout << hi << " "<< llk() << " hi\n";

	*pParam = min;
	set_priors();
	double minllk = llk();
	
	start = (test+min)/2;
	if (minllk < thold) {
		convlo = golden_section_ci(min, start, test, tau, thold, pParam);
		lo = *pParam;
	} else {
		convlo = 2;
		lo = min;
	}
	cout << lo << " "<< llk() << " lo\n";
	pair<int, int> conv = make_pair(convlo, convhi);
	pair<double, double> ci = make_pair(lo, hi);
	return make_pair(conv, ci);
}

// pair< pair<int, int>, pair<double, double> > SNPs::get_cis_lambda(int which){
// 	double startlk = llk();
// 	double thold = startlk - 2;
// 	cout <<  startlk << " "<< thold << "\n";
// 	double min = -20.0;
// 	double max = 20.0;
// 	double test = lambdas[which];
// 	if (test > max) max = test+20.0;
// 	if (test < min) min = test-20.0;
// 	if (max < 0) max = 20.0;
// 	if (min > 0) min = -20.0;
// 	double start = (test+max)/2;
// 	double tau = 0.001;
// 	double hi, lo;
// 	int convhi, convlo;
// 
// 
// 	//test if maximum is less than thold
// 	lambdas[which] = max;
// 	set_priors();
// 	double maxllk = llk();
// 	if (maxllk < thold){
// 		convhi = golden_section_lambda_ci(test, start, max, tau, which, thold);
// 		hi = lambdas[which];
// 	}
// 	else{
// 		convhi = 2;
// 		hi = max;
// 	}
// 	cout << hi << " "<< llk() << " hi\n";
// 
// 	lambdas[which] = min;
// 	set_priors();
// 	double minllk = llk();
// 	
// 	start = (test+min)/2;
// 	if (minllk < thold){
// 		convlo = golden_section_lambda_ci(min, start, test, tau, which, thold);
// 		lo = lambdas[which];
// 	}
// 	else{
// 		convlo = 2;
// 		lo = min;
// 	}
// 	cout << lo << " "<< llk() << " lo\n";
// 	pair<int, int> conv = make_pair(convlo, convhi);
// 	pair<double, double> ci = make_pair(lo, hi);
// 	return make_pair(conv, ci);
// }
// 
// pair< pair<int, int>, pair<double, double> > SNPs::get_cis_seglambda(int which){
// 	double startlk = llk();
// 	double thold = startlk - 2;
// 	cout <<  startlk << " "<< thold << "\n";
// 	double min = -20.0;
// 	double max = 20.0;
// 	double test = seglambdas[which];
// 	if (test > max) max = test+20.0;
// 	if (test < min) min = test-20.0;
// 	if (max < 0) max = 20.0;
// 	if (min > 0) min = -20.0;
// 	double start = (test+max)/2;
// 	double tau = 0.001;
// 	int convhi, convlo;
// 	double hi, lo;
// 
// 
// 	// test hi
// 	seglambdas[which] = max;
// 	set_priors();
// 	double maxllk = llk();
// 	if (maxllk < thold){
// 		convhi = golden_section_seglambda_ci(test, start, max, tau, which, thold);
// 		hi = seglambdas[which];
// 	}
// 	else{
// 		convhi = 2;
// 		hi = max;
// 	}
// 	cout << hi << " "<< llk() << " hi\n";
// 
// 	seglambdas[which] = min;
// 	set_priors();
// 	double minllk = llk();
// 
// 	start = (test+min)/2;
// 	if (minllk < thold){
// 		convlo = golden_section_seglambda_ci(min, start, test, tau, which, thold);
// 		lo = seglambdas[which];
// 	}
// 	else{
// 		convlo = 2;
// 		lo = max;
// 	}
// 	cout << lo << " "<< llk() << " lo\n";
// 	pair<int, int> conv = make_pair(convlo, convhi);
// 	pair<double, double> ci = make_pair(lo, hi);
// 	return make_pair(conv, ci);
// }

void SNPs::print(){
	cout << "rs chr pos BF Z";
	for (int i=0; i < nannot; i++) cout << " "<< annotnames[i];
	for (int i=0; i < quantannotnames.size(); i++) cout << " "<< quantannotnames[i];
	cout << "\n";
	for (vector<SNP>::iterator it = d.begin(); it != d.end(); it++){
		cout << it->id << " "<< it->chr << " "<< it->pos << " "<< it->BF <<  " "<< it->Z;
		for (int i = 0; i < nannot; i++) cout << " "<< it->annot[i];
		for (int i = 0; i < quantannotnames.size(); i++) {
			if (it->qannotDefined[i]) {
				cout << " NA";
			} else {
				cout << " " << it->qannot[i];
			}
		}
		cout << "\n";
	}
}

void SNPs::print(string outfile, string outfile2){
	ogzstream out(outfile.c_str());
	ogzstream out2(outfile2.c_str());
	print_header(out, out2);

	for (int i = 0; i < segments.size(); i++){
		print(i, out, out2);
	}
}

void SNPs::print_header(ogzstream& outSNP, ogzstream& outSeg){
	outSNP << "id chr pos logBF Z V pi pseudologPO pseudoPPA PPA chunk";
	outSeg << "chunk NSNP chr st sp max_abs_Z logBF pi logPO PPA";
	for (vector<string>::iterator it = annotnames.begin(); it != annotnames.end(); it++) outSNP << " "<< *it;
	for (vector<string>::iterator it = quantannotnames.begin(); it != quantannotnames.end(); it++) outSNP << " "<< *it;
	outSNP << "\n";
	for (vector<string>::iterator it = segannotnames.begin(); it != segannotnames.end(); it++) outSeg << " "<< *it;
	outSeg << "\n";
}

void SNPs::print(int segnum, ogzstream& outSNP, ogzstream& outSeg){
	pair<int, int> seg = segments[segnum];
	int stindex = seg.first;
	int spindex = seg.second;

	outSeg << segnum << " " << spindex-stindex << " "<< d[stindex].chr << " "<< d[stindex].pos << " "<< d[spindex-1].pos << " ";
	double segp = segpriors[segnum];
	double seglpio = log(segp)- log(1-segp);
	double seglPO;
	double logsegbf = -1000;
	double segPPA;
	double sum = 0;
	double maxZ = 0;
	for (int i = stindex; i < spindex; i++){
		double logpi = snppri[i];
		double logbf = d[i].BF;
		double Z = fabs(d[i].Z);
		if (Z> maxZ) maxZ = Z;
		logsegbf= sumlog(logsegbf, logpi+logbf);
	}
	seglPO = logsegbf+ seglpio;
	segPPA = exp(seglPO- sumlog(0, seglPO));
	//if fine mapping, all priors are 1
	if (params->finemap){
		segp = 1;
		segPPA = 1;
	}
	outSeg << maxZ<< " "<< logsegbf << " " << segp << " "<< seglPO << " "<< segPPA;
	for (int i = 0; i < nsegannot; i++) outSeg << " "<< segannot[segnum][i];
	outSeg << "\n";
	for (int i =stindex ; i < spindex; i++){
		//double pi = snppri[i]*segpi;
		double logpi = snppri[i]+log(segp);
		double lognum = snppri[i] +d[i].BF;
		double lpio = logpi - log(1-exp(logpi));
		double cPPA = exp(lognum - logsegbf);
		double lPO = d[i].BF + lpio;
		double tPPA = cPPA*segPPA;
		double PPA = exp(lPO)/  ( 1+ exp(lPO));
		outSNP << d[i].id << " "<< d[i].chr << " "<< d[i].pos << " "<< d[i].BF <<  " "<< d[i].Z <<  " " << d[i].V << " "<< snppri[i] << " "<< lPO  << " "<< PPA << " " << tPPA << " "<< segnum;
		for (int j = 0; j < annotnames.size(); j++) outSNP << " "<< d[i].annot[j];
		for (int j = 0; j < quantannotnames.size(); j++) {
			if (d[i].qannotDefined[j]) {
				outSNP << " NA";
			} else {
				outSNP << " " << d[i].qannot[j];
			}
		}
		outSNP << "\n";
	}
}

vector<double> SNPs::cross10(bool penalize, ostringstream& ostr, string outfileSNPs, string outfileSegs){
	//do 10-fold cross validation
	//
	// split segments into groups
	// L = 1/10* \sum_i L*(i)
	// where L*(i) is the likelihood of data in group i after optimizing model without it
	//
	ogzstream outSNP, outSeg;
	if (!outfileSNPs.empty() && !outfileSegs.empty()) {
		outSNP.open(outfileSNPs.c_str(), std::ios::out);
		outSeg.open(outfileSegs.c_str(), std::ios::out);
		this->print_header(outSNP, outSeg);
	}
	
	vector< set<int> > split10 = make_cross10();
	vector<double> Lstar;
	int fold = 1;
	for (vector<set<int> >::iterator it = split10.begin(); it != split10.end(); it++, fold++){
		GSL_xv_optim(*it, penalize);
		double tmpllk = 0;
		for (set<int>::iterator it2 = it->begin(); it2 != it->end(); it2++)
			tmpllk += llk(*it2);
		//cout << tmpl << "\n";
		Lstar.push_back(tmpllk);
		
		ostr << "Fold " << fold << ": Segments " << *(it->begin()) << "-" << *(it->rbegin()) << endl;
		for (int i = 0; i < seglambdas.size(); i++) ostr << segannotnames[i] << " " << seglambdas[i] << endl;
		for (int i = 0; i < lambdas.size(); i++) ostr << annotnames[i] << " " << lambdas[i] << endl;
		ostr << "QuantParam lambda b0 b1\n";
		for (int i = 0; i < quantparams.size(); i++) ostr << quantannotnames[i] << " " << quantparams[i].lambda << " " << quantparams[i].b0 << " " << quantparams[i].b1 << endl;
		
		if (!outfileSNPs.empty() && !outfileSegs.empty()) {
			for (set<int>::iterator it2 = it->begin(); it2 != it->end(); it2++)
				this->print(*it2, outSNP, outSeg);
		}
	}
	return Lstar;
}

vector<set<int> > SNPs::make_cross10(){
	vector<set<int> > toreturn;
	int nper = ceil((double) segments.size() / 10.0);
	for (int i = 0; i < 10; i++){
		set<int> tmp;
		for (int j = i*nper; j < i*nper+nper && j < segments.size(); j++) tmp.insert(j);
		toreturn.push_back(tmp);
	}
	//for (vector<set<int> >::iterator it = toreturn.begin(); it != toreturn.end(); it++){
	//	for (set<int>::iterator it2 = it->begin(); it2 != it->end(); it2++){
	//		cout << *it2 << " ";
	//	}
	//	cout << "\n";
	//}
	return toreturn;

}

map<string, vector<pair< int, int> > > SNPs::read_bedfile(string bedfile){
	map<string, vector<pair< int, int> > > toreturn;
	ifstream in(bedfile.c_str());
	vector<string> line;
	struct stat stFileInfo;
	int intStat;
	string st, buf;

	intStat = stat(bedfile.c_str(), &stFileInfo);
	if (intStat !=0){
		std::cerr<< "ERROR: cannot open file " << bedfile << "\n";
		exit(1);
	}
    while(getline(in, st)){
    	buf.clear();
    	stringstream ss(st);
    	line.clear();
    	while (ss>> buf){
    		line.push_back(buf);
    	}
    	string chr = line[0];
    	int start = atoi(line[1].c_str());
    	int stop = atoi(line[2].c_str());
    	if (toreturn.find(chr) == toreturn.end()) {
    		vector<pair<int, int> > tmp;
    		toreturn.insert(make_pair(chr, tmp));
    	}
    	toreturn[chr].push_back(make_pair(start, stop));

    }

	// check to make sure all entries are ordered

	for (map<string, vector<pair<int, int> > >::iterator it = toreturn.begin(); it != toreturn.end(); it++){
		vector<pair<int, int> > tmp = it->second;
		pair<int, int> prev = tmp[0];
		if (prev.second < prev.first) {
				cerr<<"ERROR: start and stop positions in bed file out of order: "<< it->first << " "<< prev.first<< " "<< prev.second << "\n";
				exit(1);
		}
		for (int i = 1; i < tmp.size(); i++){
			pair<int, int> current = tmp[i];
			//cout << current.first << " "<< current.second << " "<< prev.second << "\n";
			if (current.second < current.first) {
					cerr<<"ERROR: start and stop positions in bed file out of order: "<< it->first << " "<< current.first<< " "<< current.second << "\n";
					exit(1);
			}
			if (current.first < prev.second) {
						cerr<<"ERROR: bed file out of order: "<< it->first << " "<< prev.first<< " "<< prev.second << "\n" << it->first << " "<< current.first<< " "<< current.second << "\n";
						exit(1);
			}
			prev =tmp[i];
		}
	}
	return toreturn;

}
void SNPs::make_segments(string bedfile){
	segments.clear();
	map<string, vector<pair<int, int> > > bedsegs = read_bedfile(bedfile);
	for (int i = 0; i < chrnames.size(); i++){
		string tmpchr = chrnames[i];
		//cout << tmpchr << "\n";
		if (bedsegs.find(tmpchr) == bedsegs.end()){
			cerr << "ERROR: chromsome "<< tmpchr << " not found in .bed file\n";
			exit(1);
		}
		vector<pair<int, int> > intervals = bedsegs[tmpchr];
		pair<int, int> chromosome = chrsegments[i];
		pair<int, int> currentseg = intervals[0];
		int j = chromosome.first;
		int start = j;
		int intervalindex = 0;
		while (j < chromosome.second){
			int jpos = d[j].pos;

			// make sure the position is inside the segment
			if (jpos < currentseg.first){
				cerr << "ERROR: current segment is "<< currentseg.first << " "<< currentseg.second << ", position is "<< jpos << "\n";
				exit(1);
			}

			// if the first position is not in the first segment, increment the segment index
			else if (jpos > currentseg.second and j == start){
				intervalindex++;

				//if this means the first position is beyond the bed file, exit
				if (intervalindex >= intervals.size()){
					cerr << "ERROR: position "<< jpos << " is outside range of bed file\n";
					exit(1);
				}
				currentseg = intervals[intervalindex];
			}

			// inside the segment, increment position
			else if (jpos >=currentseg.first and jpos < currentseg.second) {
				j = j+1;
			}

			// past the end of the segment, add the previous segment and increment
			else if (jpos >= currentseg.second and j != start){
				segments.push_back(make_pair(start, j));
				start = j;
				intervalindex++;
				if (intervalindex >= intervals.size()){
					cerr << "ERROR: position "<< jpos << " is outside range of bed file\n";
					exit(1);
				}
				currentseg = intervals[intervalindex];
				//j = j+1;
			}
			else{
				cerr << "ERROR: missing something when reading bed? "<< jpos << " "<<  currentseg.first << " "<< currentseg.second << "\n";
				exit(1);
			}


		}
		//cout << start << " "<< j << " done adding\n";
		segments.push_back(make_pair(start, j));
	}
}
void SNPs::make_segments(int size){
	segments.clear();
	int counter = 0;
	for (vector<pair<int, int> >::iterator it = chrsegments.begin(); it != chrsegments.end(); it++){
		int starti = it->first;
		int endi = it->second;
		int length = endi-starti;
		if (length < size){
			cerr << "ERROR: chromosome "<< d[starti].chr << " has "<< length << " SNPs, requesting blocks of size "<< size << "\n";
			exit(1);
		}
		int bestmod = length % size;
		int bestsize = size;
		for (int i = size -20; i < size+20; i++){
			if (i < 1) continue;
			int test = length % i;
			if (test < bestmod){
				bestmod = test;
				bestsize = i;
			}
		}
		int nseg = length/bestsize;
		for (int i = 0; i < nseg; i++){
			int sstart = starti+i*bestsize;
			int send = starti+i*bestsize+bestsize;
			if (i > (nseg-2))	send = endi;
			segments.push_back(make_pair(sstart, send));
			for (int i = sstart ; i < send ; i++) d[i].chunknumber = counter;
			counter++;
		}
	}
}

void SNPs::make_segments_finemap(){
	//cout << "here\n"; cout.flush();
	segments.clear();
	int sstart = 0;
	int wseg = d[0].chunknumber;
	for (int i = 1; i < d.size(); i++){
		int testseg = d[i].chunknumber;
		if (testseg < wseg){
			cerr<< "ERROR: segment number "<< testseg << " occurs after "<< wseg << ". For fine-mapping, order the input file by SEGNUMBER.\n";
			exit(1);
		}
		if (testseg > wseg) {
			segments.push_back(make_pair(sstart, i));
			sstart = i;
			wseg = testseg;
		}
	}
	segments.push_back(make_pair(sstart, d.size()));
}

void SNPs::make_chrsegments(){
	chrnames.clear();
	chrsegments.clear();
	int i = 0;
	int start = i;
	int startpos = d[i].pos;
	string startchr = d[i].chr;
	while (i < d.size()){
		int tmppos = d[i].pos;
		string tmpchr = d[i].chr;
		if (tmpchr != startchr){
			int end = i;
			chrnames.push_back(startchr);
			chrsegments.push_back(make_pair(start, end));
			start = i;
			startpos = d[i].pos;
			startchr = d[i].chr;
		}
		i++;
	}
	int end = i;
	chrnames.push_back(startchr);
	chrsegments.push_back(make_pair(start, end));
}

void SNPs::print_segments(){
	for (vector<pair<int, int> >::iterator it = segments.begin(); it != segments.end(); it++){
		cout << it->first << " "<< it->second << "\n";
	}
}

void SNPs::print_chrsegments(){
	for (vector<pair<int, int> >::iterator it = chrsegments.begin(); it != chrsegments.end(); it++){
		cout << it->first << " "<< it->second << "\n";
	}
}

void SNPs::set_priors(){
	set_segpriors(); // a bit of computation for nothing if there's no segment annotations, spot for speed improvement if necessary
	for (int i = 0; i < segments.size(); i++) set_priors(i);
}

void SNPs::set_priors_cond(){
	set_segpriors();
	for (int i = 0; i < segments.size(); i++) set_priors_cond(i);
}

void SNPs::set_segpriors(){
	for (int i = 0; i < segments.size(); i++){
		double logitprior = log(segpi) - log(1-segpi);
		for (int j = 0; j < nsegannot; j++){
			if (segannot[i][j]) logitprior += seglambdas[j];
		}
		double prior = 1.0/  (1.0 + exp(-logitprior));
		segpriors[i] = prior;
	}
}

void SNPs::set_priors(int which){
	//
	// prior on SNP is exp(x_i)/ sum_j (exp(x_j))
	//

	pair<int, int> seg = segments[which];
	int st = seg.first;
	int sp = seg.second;

	double sumxs = d[st].get_x(lambdas, quantparams);
	snppri[st] = sumxs;
	for (int i = st+1; i < sp ;i++) {
		double tmpx = d[i].get_x(lambdas, quantparams);
		snppri[i] = tmpx;
		sumxs = sumlog(sumxs, tmpx);
	}
	for (int i = st; i < sp ; i++) {
		//doing this in log space
		snppri[i] = snppri[i] - sumxs;
		if (!isfinite(snppri[i])){
			cerr << "ERROR: prior for SNP "<< i << " is " << snppri[i] << "\n";
			exit(1);
		}
	}
}

void SNPs::set_priors_cond(int which){
	pair<int, int> seg = segments[which];
	int st = seg.first;
	int sp = seg.second;
	double sumxs = 0;
	for (int i = st; i < sp ;i++) {
		//cout << i << " "<< d[i].get_x(lambdas) << "\n";
		//double tmpx = exp(d[i].get_x(lambdas));
		double tmpx = d[i].get_x_cond(lambdas, quantparams, condlambda);
		snppri[i] = tmpx;
		//sumxs += tmpx;
		sumxs = sumlog(sumxs, tmpx);
	}
	for (int i = st; i <  sp ; i++) {
		//snppri[i] = snppri[i]/sumxs;
		//doing this in log space
		snppri[i] = snppri[i] - sumxs;
		if (!isfinite(snppri[i])){
			cerr << "ERROR: prior for SNP "<< i << " is " << snppri[i] << "\n";
			exit(1);
		}
		//if (snppri[i] < DBL_MIN) snppri[i] = DBL_MIN;
		//cout << i << " "<< snppri[i] << "\n";
	}
}

static const double lostOptimRidgePenalty = 0.001;

double SNPs::llk(){
	return llk(set<int>(), false);
}

double SNPs::llk(set<int> skip, bool penalize){
	double toreturn = 0;
	for (int i = 0; i < segments.size(); i++) {
		if (skip.find(i) != skip.end()) continue;
		toreturn += llk(i);
	}
	if (penalize && params->ridge_penalty > 0){
		double p = params->ridge_penalty;
		for (vector<double>::iterator it = lambdas.begin(); it != lambdas.end(); it++)			toreturn -= p * *it * *it;
		for (vector<QuantParams>::iterator it = quantparams.begin(); it != quantparams.end(); it++)	toreturn -= p * (it->lambda * it->lambda + it->b0 * it->b0 + it->b1 * it->b1);
		//for (vector<QuantParams>::iterator it = quantparams.begin(); it != quantparams.end(); it++)	toreturn -= p * (it->lambda * it->lambda);
		for (vector<double>::iterator it = seglambdas.begin(); it != seglambdas.end(); it++)	toreturn -= p * *it * *it;
	} else {
		// Because quantitative annotations are defined for every SNP, sometimes the optimization
		// gets lost tracking along a ridge where you can increase lambda indefinitely at the expense
		// of another parameter in the logistic equation. We can stop this by putting a very small
		// ridge penalty that is negligible for reasonable values of lambda.
		for (vector<QuantParams>::iterator it = quantparams.begin(); it != quantparams.end(); it++)	toreturn -= lostOptimRidgePenalty * (it->lambda * it->lambda + it->b0 * it->b0 + it->b1 * it->b1);
	}
	//data_llk = toreturn;
	return toreturn;
}


double SNPs::llk(int which){
	double toreturn;
	pair<int, int> seg = segments[which];

	int st = seg.first;
	int sp = seg.second;
	double lsum = snppri[st]+ d[st].BF;
	st++;
	for (int i = st; i < sp ; i++){
		double tmp2add = snppri[i]+ d[i].BF;
		//double tmp2add = log(snppri[i])+ d[i].BF;
		if (!isfinite(tmp2add)){
			cerr << "ERROR: likelihood of "<< tmp2add << " at SNP "<< d[i].id << " BF:"<< d[i].BF << " "<< snppri[i] << "\n";
			exit(1);
		}
		lsum  = sumlog(lsum, tmp2add);
		//cout << tmp2add << " "<< snppri[i]<< " "<< d[i].BF << " "<< lsum << "\n";
	}
	if (params->finemap) return lsum;
	toreturn = log(segpriors[which]) + lsum;
	//cout << toreturn << "\n";
	toreturn = sumlog(toreturn, log(1-segpriors[which]));

	return toreturn;
}

double SNPs::sumlog(double logx, double logy){
	if (logx > logy) return logx + log(1 + exp(logy-logx));
	else return logy + log(1 + exp(logx-logy));
}

void SNPs::set_post(){
	for (int i = 0; i < segments.size(); i++) set_post(i);
}
void SNPs::set_post(int which){
	pair<int, int> seg = segments[which];
	int st = seg.first;
	int sp = seg.second;
	double seglk = llk(which);
	for (int i = st; i < sp; i++){
		double num = log(segpi) + d[i].BF + snppri[i];
		//double num = log(segpi) + d[i].BF + log(snppri[i]);
		double lpost = num - seglk;
		snppost[i] = exp(lpost);
	}
}

void SNPs::print_segprobs(string outfile){
	ogzstream out(outfile.c_str());
	for (vector<pair<int, int> >::iterator it = segments.begin(); it != segments.end(); it++){
		int st = it->first;
		int sp = it->second;
		double total = 0;
		for (int i = st ; i < sp ; i++){
			total += snppost[i];
		}
		out << d[st].chr << " "<< d[st].pos << " "<< d[sp].pos << " " <<  total << "\n";
	}
}

void SNPs::optimize_segpi(){
	double start = log(segpi) - log(1-segpi);
	double min = -10.0;
	double max = 10.0;
	double tau = 0.001;
	golden_section_segpi(min, start, max, tau);
	cout << segpi << "\n";
}

void SNPs::optimize_condlambda(){
	double start = 0;
	double min = -20.0;
	double max = 20.0;
	double tau = 0.001;
	golden_section_condlambda(min, start, max, tau);
	cout << condlambda << "\n";
}

void SNPs::optimize_l0(){
	double start = lambdas[0];
	double min = -1000.0;
	double max = 1000.0;
	double tau = 0.001;
	golden_section_l0(min, start, max, tau);
	cout << lambdas[0] << "\n";
}

void SNPs::GSL_optim(){
	GSL_optim(&GSL_llk, set<int>(), false);
}

void SNPs::GSL_optim_ridge(){
	GSL_optim(&GSL_llk, set<int>(), true);
}

void SNPs::GSL_xv_optim(set<int> toskip, bool penalize){
	GSL_optim(&GSL_llk, toskip, penalize);
}

void SNPs::GSL_optim(LLKFunction* pLLKFunc, set<int> toskip, bool penalize){
	int nparam = nannot + (quantModelParamNum * quantparams.size());
	if (nsegannot > 0) {
		if (params->finemap) {
			cerr << "ERROR: There should be no region-level annotations in fine-mapping mode\n";
			exit(1);
		}
		// Also include the segpi parameter
		nparam += nsegannot + 1;
	}
	if (nparam < 1) return;
	size_t iter = 0;
	double size;
	int status;
	const gsl_multimin_fminimizer_type *T =
			gsl_multimin_fminimizer_nmsimplex2;
	gsl_multimin_fminimizer *s;
	gsl_vector *x;
	gsl_vector *ss;
	gsl_multimin_function lm;
	lm.n = nparam;
	lm.f = pLLKFunc;
	struct GSL_params p;
	p.d = this;
	p.toskip = toskip;
	p.penalize = penalize;
	lm.params = &p;
	//cout << llk()<< "\n"; cout.flush();
	//
	// initialize parameters
	//
	x = gsl_vector_alloc(nparam);
	int curParam = 0;
	if (nsegannot > 0) {
		gsl_vector_set(x, curParam, log(segpi) - log(1-segpi));
		curParam++;
	}
	for (; curParam < nparam; curParam++) {
		gsl_vector_set(x, curParam, 1);
	}

	// set initial step sizes to 1
	ss = gsl_vector_alloc(nparam);
	gsl_vector_set_all(ss, 1.0);
	s = gsl_multimin_fminimizer_alloc (T, nparam);

	int numIterationsStuck = 0;
	double lastSize = 0;
	double lastLLK = 0;
	vector<double> llks;
	gsl_multimin_fminimizer_set (s, &lm, x, ss);
	do
	{
		iter++;
		status = gsl_multimin_fminimizer_iterate (s);
		
		if (status){
			printf ("error: %s\n", gsl_strerror (status));
			break;
		}
		
		size = gsl_multimin_fminimizer_size(s);
		status = gsl_multimin_test_size (size, 0.001);
		//cout << iter << " "<< iter %10 << "\n";
		//if (iter % 20 < 1 || iter < 20){
		cout << "iteration: " << iter;
		if (nsegannot > 0) {
			cout << " " << segpi;
			for (int i = 0; i < nsegannot; i++) cout <<  " " << seglambdas[i];
		}
		for (int i = 0; i < nannot; i++) cout << " " << lambdas[i];
		for (int i = 0; i < quantparams.size(); i++) cout << " " << quantparams[i].lambda << "," << quantparams[i].b0 << "," << quantparams[i].b1;
		cout << " "<< s->fval << " "<< size << "\n" << flush;

		llks.push_back(s->fval);
		// Stop optimisation if the rate of improvement of LLK is too low
		if (iter > 200) {
			double llkDiff = abs(llks[iter-1] - llks[iter-201]);
			// Require that LLK improve at least 1 unit over the above number of iterations
			if (llkDiff < 0.2) {
				cout << "WARNING: optimization improvement is lower than 0.2 unit LLK over 200 iterations. Ending optimization.\n";
				break;
			}
		}
		if (s->fval == lastLLK && size == lastSize) {
			numIterationsStuck++;
			if (numIterationsStuck > (2*nparam + 10)) {
				cout << "WARNING: optimization stuck at the same values for too many iterations (" << (2*nparam + 10) << ").\n";
				cout << "WARNING: failed to converge\n";
				break;
			}
		} else {
			numIterationsStuck = 0;
			lastSize = size;
			lastLLK = s->fval;
		}
	}
	while (status == GSL_CONTINUE && iter < 5000);
	if (iter > 4999) {
		cerr << "WARNING: failed to converge\n";
		//exit(1);
	}
	curParam = 0;
	if (nsegannot > 0) {
		segpi = 1.0 /  (1.0 + exp (- gsl_vector_get(s->x, curParam)));
		curParam++;
	}
	for (int i = 0; i < nsegannot; i++) seglambdas[i] = gsl_vector_get(s->x, curParam+i);
	curParam += nsegannot;
	
	for (int i = 0; i < nannot; i++) lambdas[i] = gsl_vector_get(s->x, curParam+i);
	curParam += nannot;
 
	for (int i = 0; i < quantparams.size(); i++) {
		int index = curParam + i*quantModelParamNum;
		quantparams[i].lambda = gsl_vector_get(s->x, index);
		quantparams[i].b0 = gsl_vector_get(s->x, index+1);
		if (quantModelParamNum > 2) {
			quantparams[i].b1 = gsl_vector_get(s->x, index+2);
		}
	}

	gsl_multimin_fminimizer_free(s);
	gsl_vector_free(x);
	gsl_vector_free(ss);
}


int SNPs::golden_section_segpi(double min, double guess, double max, double tau){
        double x;

        if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
        else x = guess - resphi *(guess-min);
        if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
                double new_segpi = (min+max)/2;
                segpi =  1.0  / ( 1.0 + exp(-new_segpi));
                data_llk = llk();
                return 0;
        }

        segpi = 1.0  / ( 1.0 + exp(-x));
        double f_x = -llk();

        segpi =  1.0  / ( 1.0 + exp(-guess));
        double f_guess = -llk();
        cout << x << " " <<  guess << " "<< segpi << " "<< f_x << " "<< f_guess <<  "\n";
        if (f_x < f_guess){
                if ( (max-guess) > (guess-min) )        return golden_section_segpi(guess, x, max, tau);
                else return golden_section_segpi(min, x, guess, tau);
        }
        else{
                if ( (max - guess) > (guess - min)  ) return golden_section_segpi(min, guess, x, tau);
                else return golden_section_segpi(x, guess, max, tau);
        }
}

int SNPs::golden_section_condlambda(double min, double guess, double max, double tau){
        double x;
        if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
        else x = guess - resphi *(guess-min);
        if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
                double new_segpi = (min+max)/2;
                condlambda =  new_segpi;
                set_priors_cond();
                data_llk = llk();
                return 0;
        }

        condlambda= x;
        set_priors_cond();
        double f_x = -llk();

        condlambda = guess;
        set_priors_cond();
        double f_guess = -llk();

        cout << x << " " <<  guess << " "<< f_x << " "<< f_guess  << " "<< max << " "<< min << "\n";
        if (f_x < f_guess){
                if ( (max-guess) > (guess-min) )        return golden_section_condlambda(guess, x, max, tau);
                else return golden_section_condlambda(min, x, guess, tau);
        }
        else{
                if ( (max - guess) > (guess - min)  ) return golden_section_condlambda(min, guess, x, tau);
                else return golden_section_condlambda(x, guess, max, tau);
        }
}

int SNPs::golden_section_condlambda_ci(double min, double guess, double max, double tau, double target){
        double x;

        if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
        else x = guess - resphi *(guess-min);
        if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
                double new_segpi = (min+max)/2;
                condlambda =  new_segpi;
                set_priors_cond();
                data_llk = llk();
                double tmpdiff = data_llk- target;
                double d2 = tmpdiff*tmpdiff;
                if (d2 > tau) return 1;
                else return 0;
        }

        condlambda = x;
        set_priors_cond();
        double f_x = llk()-target;
        f_x = f_x*f_x;
       // double x_llk = llk();

        condlambda = guess;
        set_priors_cond();
        double f_guess = llk()-target;
        f_guess = f_guess*f_guess;
       // double guess_llk = llk();
        cout << x << " " <<  guess << " "<< f_x << " "<< f_guess  << " "<< max << " "<< min << "\n";
        if (f_x < f_guess){
                if ( (max-guess) > (guess-min) )        return golden_section_condlambda_ci(guess, x, max, tau, target);
                else return golden_section_condlambda_ci(min, x, guess, tau, target);
        }
        else{
                if ( (max - guess) > (guess - min)  ) return golden_section_condlambda_ci(min, guess, x, tau, target);
                else return golden_section_condlambda_ci(x, guess, max, tau, target);
        }
}

int SNPs::golden_section_ci(double min, double guess, double max, double tau, double target, double* pParam) {
	double x;

	if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
	else x = guess - resphi *(guess-min);
	if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
		double new_segpi = (min+max)/2;
		*pParam = new_segpi;
		set_priors();
		data_llk = llk();
		double tmpdiff = data_llk- target;
		double d2 = tmpdiff*tmpdiff;
		if (d2 > tau) return 1;
		else return 0;
	}

	*pParam = x;
	set_priors();
	double f_x = llk()-target;
	f_x = f_x*f_x;
	// double x_llk = llk();

	*pParam = guess;
	set_priors();
	double f_guess = llk()-target;
	f_guess = f_guess*f_guess;
	// double guess_llk = llk();
	cout << x << " " <<  guess << " "<< f_x << " "<< f_guess  << " "<< max << " "<< min << "\n";
	if (f_x < f_guess){
		if ( (max-guess) > (guess-min) )        return golden_section_ci(guess, x, max, tau, target, pParam);
		else return golden_section_ci(min, x, guess, tau, target, pParam);
	}
	else{
		if ( (max - guess) > (guess - min)  ) return golden_section_ci(min, guess, x, tau, target, pParam);
		else return golden_section_ci(x, guess, max, tau, target, pParam);
	}
}

// int SNPs::golden_section_lambda_ci(double min, double guess, double max, double tau, int which, double target){
//         double x;
// 
//         if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
//         else x = guess - resphi *(guess-min);
//         if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
//                 double new_segpi = (min+max)/2;
//                 lambdas[which] =  new_segpi;
//                 set_priors();
//                 data_llk = llk();
//                 double tmpdiff = data_llk- target;
//                 double d2 = tmpdiff*tmpdiff;
//                 if (d2 > tau) return 1;
//                 else return 0;
//         }
// 
//         lambdas[which] = x;
//         set_priors();
//         double f_x = llk()-target;
//         f_x = f_x*f_x;
//        // double x_llk = llk();
// 
//         lambdas[which] = guess;
//         set_priors();
//         double f_guess = llk()-target;
//         f_guess = f_guess*f_guess;
//        // double guess_llk = llk();
//         cout << x << " " <<  guess << " "<< f_x << " "<< f_guess  << " "<< max << " "<< min << "\n";
//         if (f_x < f_guess){
//                 if ( (max-guess) > (guess-min) )        return golden_section_lambda_ci(guess, x, max, tau, which, target);
//                 else return golden_section_lambda_ci(min, x, guess, tau, which, target);
//         }
//         else{
//                 if ( (max - guess) > (guess - min)  ) return golden_section_lambda_ci(min, guess, x, tau, which, target);
//                 else return golden_section_lambda_ci(x, guess, max, tau, which, target);
//         }
// }
// 
// int SNPs::golden_section_seglambda_ci(double min, double guess, double max, double tau, int which, double target){
//         double x;
// 
//         if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
//         else x = guess - resphi *(guess-min);
//         if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
//                 double new_segpi = (min+max)/2;
//                 seglambdas[which] =  new_segpi;
//                 set_priors();
//                 data_llk = llk();
//                 double tmpdiff = data_llk- target;
//                 double d2 = tmpdiff*tmpdiff;
//                 if (d2 > tau) return 1;
//                 else return 0;
//         }
// 
//         seglambdas[which] = x;
//         set_priors();
//         double f_x = llk()-target;
//         f_x = f_x*f_x;
//         //double x_llk = llk();
// 
//         seglambdas[which] = guess;
//         set_priors();
//         double f_guess = llk()-target;
//         f_guess = f_guess*f_guess;
//         //double guess_llk = llk();
//         cout << x << " " <<  guess << " "<< f_x << " "<< f_guess << " "<< max << " "<< min << "\n";
//         if (f_x < f_guess){
//                 if ( (max-guess) > (guess-min) )        return golden_section_seglambda_ci(guess, x, max, tau, which, target);
//                 else return golden_section_seglambda_ci(min, x, guess, tau, which, target);
//         }
//         else{
//                 if ( (max - guess) > (guess - min)  ) return golden_section_seglambda_ci(min, guess, x, tau, which, target);
//                 else return golden_section_seglambda_ci(x, guess, max, tau, which, target);
//         }
// }

int SNPs::golden_section_segpi_ci(double min, double guess, double max, double tau, double target, int * nit){
        double x;
        *nit = *nit +1;
        if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
        else x = guess - resphi *(guess-min);
        if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
                double new_segpi = (min+max)/2;
                segpi =  1.0  / ( 1.0 + exp(-new_segpi));
                set_priors();
                data_llk = llk();
                double tmpdiff = data_llk- target;
                double d2 = tmpdiff*tmpdiff;
                if (d2 > tau) return 1;
                else return 0;
        }
        else if (*nit > 50) return 1;

        segpi = 1.0  / ( 1.0 + exp(-x));
        set_priors();
        double f_x = llk()-target;
        f_x = f_x*f_x;

        segpi =  1.0  / ( 1.0 + exp(-guess));
        set_priors();
        double f_guess = llk()- target;
        f_guess = f_guess*f_guess;

        cout << x << " " <<  guess << " "<< f_x << " "<< f_guess <<  " "<< max << " "<< min << " "<< target << "\n";
        if (f_x < f_guess){
                if ( (max-guess) > (guess-min) )        return golden_section_segpi_ci(guess, x, max, tau, target, nit);
                else return golden_section_segpi_ci(min, x, guess, tau, target, nit);
        }
        else{
                if ( (max - guess) > (guess - min)  ) return golden_section_segpi_ci(min, guess, x, tau, target, nit);
                else return golden_section_segpi_ci(x, guess, max, tau, target, nit);
        }
}

int SNPs::golden_section_l0(double min, double guess, double max, double tau){
        double x;

        if ( (max - guess) > (guess - min)) x = guess + resphi *( max - guess);
        else x = guess - resphi *(guess-min);
        if (fabs(max-min) < tau * (fabs(guess)+fabs(max))) {
                double new_segpi = (min+max)/2;
                lambdas[0] =  new_segpi;
                data_llk = llk();
                return 0;
        }

        lambdas[0] = x;
        set_priors();
        double f_x = -llk();

        lambdas[0] = guess;
        set_priors();
        double f_guess = -llk();
        cout << x << " " <<  guess << " "<< segpi << " "<< f_x << " "<< f_guess <<  " "<< max << " "<< min << "\n";
        if (f_x < f_guess){
                if ( (max-guess) > (guess-min) )        return golden_section_l0(guess, x, max, tau);
                else return golden_section_l0(min, x, guess, tau);
        }
        else{
                if ( (max - guess) > (guess - min)  ) return golden_section_l0(min, guess, x, tau);
                else return golden_section_l0(x, guess, max, tau);
        }
}

double GSL_llk(const gsl_vector *x, void *params){
	struct GSL_params *gslparams = (struct GSL_params *) params;
	SNPs* d = gslparams->d;
	int na = d->nannot;
	int ns = d->nsegannot;
	int nq = d->quantparams.size();
	int curParam = 0;
	if (ns > 0) {
		d->segpi = 1.0 / (1.0 + exp (- gsl_vector_get(x, 0)));
		curParam += 1;
	}
	for (int i = 0; i < ns; i++){
		d->seglambdas[i] = gsl_vector_get(x, curParam+i);
	}
	curParam += ns;
	for (int i = 0; i < na; i++){
		d->lambdas[i] = gsl_vector_get(x, curParam+i);
	}
	curParam += na;
	for (int i = 0; i < nq; i++) {
		int index = curParam + i*d->quantModelParamNum;
		d->quantparams[i].lambda = gsl_vector_get(x, index);
		d->quantparams[i].b0 = gsl_vector_get(x, index+1);
     	if (d->quantModelParamNum > 2) {
			d->quantparams[i].b1 = gsl_vector_get(x, index+2);
		}
	}
	d->set_priors();
	return -d->llk(gslparams->toskip, gslparams->penalize);
}

