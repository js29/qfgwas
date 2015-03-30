/*
 * SNP.cpp
 *
 *  Created on: Jan 14, 2013
 *      Author: pickrell
 */
#include "SNP.h"
using namespace std;

SNP::SNP(){

}

void SNP::initSNP(string rs, string c, int p, int nsamp, double fr, double zscore, const vector<double>& prior, const vector<bool>& an, const vector<int>& ds, const vector<vector<pair<int, int> > >& dmodels){
	id = rs;
	chr = c;
	pos = p;
	f = fr;
	N = nsamp;
	Z = zscore;
	V = approx_v();
	W = prior;
	BF = calc_logBF();
	annot = an;
	//for (vector<bool>::const_iterator it = an.begin(); it != an.end(); it++) {
	//	if (*it) annot_weight.push_back(1.0);
	//	else annot_weight.push_back(0.0);
	//}

	//distribute weights
	//float s = 0;
	//for (vector<float>::iterator it = annot_weight.begin(); it != annot_weight.end(); it++) s += *it;
	//if (s > 0) for (int i = 0; i < annot_weight.size(); i++) annot_weight[i]  = annot_weight[i]/s;

	dists = ds;
	// append distance annotations
	append_distannots(dmodels);
	nannot = annot.size();
}

void SNP::initSNP(string rs, string c, int p, int ncases, int ncontrols, double fr, double zscore, const vector<double>& prior, const vector<bool>& an, const vector<int>& ds, const vector<vector<pair<int, int> > >& dmodels){
	id = rs;
	chr = c;
	pos = p;
	f = fr;
	Z = zscore;
	W = prior;
	Ncase = ncases;
	Ncontrol = ncontrols;
	V = approx_v_cc();

	BF = calc_logBF();
	//cout << rs << " "<< BF << "\n"; cout.flush();
	annot = an;
	//for (vector<bool>::const_iterator it = an.begin(); it != an.end(); it++) {
	//	if (*it) annot_weight.push_back(1.0);
	//	else annot_weight.push_back(0.0);
	//}

	//distribute weights
	//float s = 0;
	//for (vector<float>::iterator it = annot_weight.begin(); it != annot_weight.end(); it++) s += *it;
	//if (s > 0) for (int i = 0; i < annot_weight.size(); i++) annot_weight[i]  = annot_weight[i]/s;

	dists = ds;
	// append distance annotations
	append_distannots(dmodels);
	nannot = annot.size();
}
/*
SNP::SNP(string rs, string c, int p, double fr, double mean, double se, double prior, vector<bool> an){
	id = rs;
	chr = c;
	pos = p;
	f = fr;
	N = 0;
	Z = mean/se;
	V = se*se;
	W = prior;
	BF = calc_logBF();
	nannot = an.size();
	for (vector<bool>::iterator it = an.begin(); it != an.end(); it++) annot.push_back(*it);
}
*/

double SNP::sumlog(double logx, double logy){
        if (logx > logy) return logx + log(1 + exp(logy-logx));
        else return logy + log(1 + exp(logx-logy));
}

void SNP::append_distannots(const vector<vector<pair<int, int> > >& dmodels){
	for (int i = 0; i < dists.size(); i++){
		int dist = dists[i];
		bool found = false;
		for (vector<pair<int, int> >::const_iterator it = dmodels[i].begin(); it != dmodels[i].end(); it++){
			int st = it->first;
			int sp = it->second;
			if (dist >= st && dist < sp) {
				if (found){
					cerr << "ERROR: SNP "<< id << " is in more than one distance bin for distance measure number "<< i << "\n";
					exit(1);
				}
				annot.push_back(true);
				//annot_weight.push_back(1.0);
				found = true;
			}
			else {
				annot.push_back(false);
				//annot_weight.push_back(0.0);
			}
		}
	}
}

double SNP::calc_logBF_ind(double WW){
	double toreturn = 0;
	double r = WW/ (V+WW);
	toreturn += -log ( sqrt(1-r) );
	//cout << "tmp1 "<< toreturn << "\n";
	toreturn += - (Z*Z*r/2);
	//cout << "tmp2 "<< - (Z*Z/2.0) *( w/(V+w)) << "\n";
	return -toreturn;
}


double SNP::calc_logBF(){
	double toreturn = calc_logBF_ind(W[0]);
	for (int i = 1; i < W.size(); i++)toreturn = sumlog(toreturn, calc_logBF_ind(W[i]));
	toreturn = toreturn - log(W.size());
	return toreturn;
}

double SNP::approx_v(){
	double toreturn;
	toreturn = 2*f*(1-f) * (double) N;
	toreturn = 1.0/toreturn;
	return toreturn;
}

double SNP::approx_v_cc(){
	double toreturn;
	double maf = f;
	if (maf > 0.5) maf = 1-maf;
	double num = (double) Ncase + (double) Ncontrol;
	double tmp1 = 2*maf*(1-maf) + 4*maf*maf;
	double interior = 2*maf*(1-maf)+2*maf*maf;
	double tmp2 = interior*interior;

	double denom = (double) Ncase * (double) Ncontrol * (tmp1-tmp2);
	//cout << Ncase << " "<< Ncontrol << " "<< maf << " "<< num << " "<< denom << " "<< tmp1 << " "<< tmp2 << "\n"; cout.flush();
	toreturn = num/denom;
	//toreturn = toreturn*toreturn;
	return toreturn;
}

double SNP::get_x(const vector<double>& lambda){
	// JS: Since get_x is the most time-critical part of fgwas code, minimize
	// the number of run-time checks here.
	//if (lambda.size() != nannot){
	//	cerr << "ERROR: SNP "<< id << ". Lambda has "<< lambda.size()<< " entries. nannot is " << nannot << "\n";
	//	exit(1);
	//}
	double toreturn = 0;
	for (int i = 0; i < nannot; i++) {
		if (annot[i]) toreturn += lambda[i];
	}
	return toreturn;
}

double SNP::get_x_cond(const vector<double>& lambda, double lambdac){
	// JS: Since get_x is the most time-critical part of fgwas code, minimize
	// the number of run-time checks here.
	//if (lambda.size() != nannot){
	//	cerr << "ERROR: SNP "<< id << ". Lambda has "<< lambda.size()<< " entries. nannot is " << nannot << "\n";
	//	exit(1);
	//}
	double toreturn = 0;
	for (int i = 0; i < nannot; i++) {
		if (annot[i]) toreturn += lambda[i];
	}
	if (condannot) toreturn+= lambdac;
	return toreturn;
}

