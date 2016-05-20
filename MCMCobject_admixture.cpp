//
//  MavericK
//  MCMCobject_admixture.cpp
//
//  Created: Bob on 06/11/2015
//
//  Distributed under the MIT software licence - see Notes.c file for details
//
//  Further details (if any) of this set of functions can be found in the corresponding header file.
//
// ---------------------------------------------------------------------------

#include "MCMCobject_admixture.h"

using namespace std;

//------------------------------------------------
// MCMCobject_admixture::
// constructor for class containing all elements required for MCMC under admixture model
MCMCobject_admixture::MCMCobject_admixture(globals &globals, int _Kindex, int _burnin, int _samples, int _thinning, double _beta) {
    
    // copy some values over from globals object
    data = globals.data;
    
    Kindex = _Kindex;
    K = globals.Kmin+Kindex;
    n = globals.n;
    loci = globals.loci;
    J = globals.J;
    ploidy_vec = globals.ploidy_vec;
    uniquePops = globals.uniquePops;
    geneCopies = globals.geneCopies;
    
    lambda = globals.lambda;
    fixAlpha_on = globals.fixAlpha_on;
    alpha = globals.alpha[Kindex];
    alphaPropSD = globals.alphaPropSD[Kindex];
    beta = _beta;
    
    outputQmatrix_pop_on = globals.outputQmatrix_pop_on;
    
    burnin = _burnin;
    samples = _samples;
    thinning = _thinning;
    
    // create lookup table for log function
    int Jmax = *max_element(begin(J),end(J));
    log_lookup = vector< vector<double> >(int(1e3),vector<double>(Jmax+1));
    for (int i=0; i<int(1e3); i++) {
        for (int j=0; j<(Jmax+1); j++) {
            log_lookup[i][j] = log(double(i+j*lambda));
        }
    }
    
    linearGroup = vector<int>(geneCopies);
    
    // initialise allele counts and frequencies
    alleleCounts = vector< vector< vector<int> > >(K);
    alleleCountsTotals = vector< vector<int> >(K);
    alleleFreqs = vector< vector< vector<double> > >(K);
    for (int k=0; k<K; k++) {
        alleleCounts[k] = vector< vector<int> >(loci);
        alleleCountsTotals[k] = vector<int>(loci);
        alleleFreqs[k] = vector< vector<double> >(loci);
        for (int l=0; l<loci; l++) {
            alleleCounts[k][l] = vector<int>(J[l]);
            alleleFreqs[k][l] = vector<double>(J[l]);
        }
    }
    
    // initialise admix counts and frequencies
    admixCounts = vector< vector<int> >(n,vector<int>(K));
    admixCountsTotals = vector<int>(n);
    admixFreqs = vector< vector<double> >(n,vector<double>(K));
    
    // initialise probabilities
    logProbVec = vector<double>(K);
    logProbVecSum = 0;
    probVec = vector<double>(K);
    
    // initialise Qmatrices
    logQmatrix_gene_old = vector< vector<double> >(geneCopies, vector<double>(K));
    logQmatrix_gene_new = vector< vector<double> >(geneCopies, vector<double>(K));
    Qmatrix_gene_new = vector< vector<double> >(geneCopies, vector<double>(K));
    logQmatrix_gene_running = vector< vector<double> >(geneCopies, vector<double>(K));
    
    logQmatrix_gene = vector< vector<double> >(geneCopies, vector<double>(K));
    Qmatrix_gene = vector< vector<double> >(geneCopies, vector<double>(K));
    Qmatrix_ind = vector< vector<double> >(n, vector<double>(K));
    Qmatrix_pop = vector< vector<double> >(uniquePops.size(), vector<double>(K));
    
    // initialise objects for Hungarian algorithm
    costMat = vector< vector<double> >(K, vector<double>(K));
    bestPermOrder = vector<int>(K);
    
    edgesLeft = vector<int>(K);
    edgesRight = vector<int>(K);
    blockedLeft = vector<int>(K);
    blockedRight = vector<int>(K);
    
}

//------------------------------------------------
// MCMCobject_admixture::
// reset objects used in MCMC
void MCMCobject_admixture::reset(bool reset_Qmatrix_running) {
    
    // reset likelihoods
    logLikeGroup = 0;
    logLikeGroup_sum = 0;
    logLikeGroup_store = vector<double>(samples);
    logLikeGroup_sumSquared = 0;
    logLikeJoint = 0;
    logLikeJoint_sum = 0;
    logLikeJoint_sumSquared = 0;
    harmonic = log(double(0));
    
    // reset Qmatrices
    logQmatrix_gene_old = vector< vector<double> >(geneCopies, vector<double>(K));
    logQmatrix_gene_new = vector< vector<double> >(geneCopies, vector<double>(K));
    Qmatrix_gene_new = vector< vector<double> >(geneCopies, vector<double>(K));
    if (reset_Qmatrix_running) {
        logQmatrix_gene_running = vector< vector<double> >(geneCopies, vector<double>(K,-log(double(K))));
    }
    
    logQmatrix_gene = vector< vector<double> >(geneCopies, vector<double>(K, log(double(0))));
    Qmatrix_gene = vector< vector<double> >(geneCopies, vector<double>(K));
    Qmatrix_ind = vector< vector<double> >(n, vector<double>(K));
    Qmatrix_pop = vector< vector<double> >(uniquePops.size(), vector<double>(K));
    
    // initialise group with random allocation
    vector<double> equalK(K,1/double(K));
    groupIndex=-1;
    for (int i=0; i<n; i++) {
        for (int l=0; l<loci; l++) {
            for (int p=0; p<ploidy_vec[i]; p++) {
                groupIndex++;
                linearGroup[groupIndex] = sample1(equalK,1.0);
            }
        }
    }
    
    // zero allele counts and admix counts
    for (int k=0; k<K; k++) {
        alleleCountsTotals[k] = vector<int>(loci);
        for (int l=0; l<loci; l++) {
            alleleCounts[k][l] = vector<int>(J[l]);
        }
    }
    admixCounts = vector< vector<int> > (n,vector<int>(K));
    admixCountsTotals = vector<int> (n);
    
    // populate allele counts and admix counts
    groupIndex=-1;
    for (int ind=0; ind<n; ind++) {
        for (int l=0; l<loci; l++) {
            for (int p=0; p<ploidy_vec[ind]; p++) {
                groupIndex++;
                if (data[ind][l][p]!=0) {
                    alleleCounts[linearGroup[groupIndex]-1][l][data[ind][l][p]-1]++;
                    alleleCountsTotals[linearGroup[groupIndex]-1][l]++;
                    
                    admixCounts[ind][linearGroup[groupIndex]-1]++;
                    admixCountsTotals[ind]++;
                }
            }
        }
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// perform complete MCMC under admixture model
void MCMCobject_admixture::perform_MCMC(globals &globals, bool drawAlleleFreqs, bool storeLoglike, bool fixLabels, bool outputLikelihood, bool outputPosteriorGrouping, int mainRep) {
    
    // perform MCMC
    int thinSwitch = 1;
    for (int rep=0; rep<(burnin+samples); rep++) {
        
        // thinning loop (becomes active after burn-in)
        for (int thin=0; thin<thinSwitch; thin++) {
            
            // update group allocation of all individuals
            group_update();
            
            // if alpha not fixed update by Metropolis step
            if (globals.fixAlpha_on==0)
                alpha_update();
            
        }
        if (rep==burnin)
            thinSwitch = thinning;
        
        // if fix label-switching problem
        if (fixLabels) {
            // calculate logQmatrix_gene_new for this iteration
            produceQmatrix();
            
            // fix label-switching problem
            chooseBestLabelPermutation(globals, rep);
            
            // add logQmatrix_gene_new to logQmatrix_gene_running
            updateQmatrix(rep);
        
            // store Qmatrix values if no longer in burn-in
            if (rep>=burnin)
                storeQmatrix();
        }
            
        // calculate marginal likelihood
        d_logLikeGroup();
        
        // optionally draw allele frequencies and admixture proportions and calculate joint likelihood
        if (drawAlleleFreqs) {
            drawFreqs();
            d_logLikeJoint();
        }
        
        // add likelihoods to running sums
        if (rep>=burnin) {
            logLikeGroup_sum += logLikeGroup;
            logLikeGroup_sumSquared += logLikeGroup*logLikeGroup;
            
            if (storeLoglike) {
                logLikeGroup_store[rep-burnin] = logLikeGroup;
            }
            
            harmonic = logSum(harmonic, -logLikeGroup);
            if (drawAlleleFreqs==true) {
                logLikeJoint_sum += logLikeJoint;
                logLikeJoint_sumSquared += logLikeJoint*logLikeJoint;
            }
        }
        
        // write to outputLikelihoods file
        if (outputLikelihood) {
            globals.outputLikelihood_fileStream << K << "," << mainRep+1 << "," << rep-burnin+1 << "," << logLikeGroup << "," << logLikeJoint << "," << alpha << "\n";
			globals.outputLikelihood_fileStream.flush();
        }
        // write to outputPosteriorGrouping file
        if (outputPosteriorGrouping) {
            globals.outputPosteriorGrouping_fileStream << K << "," << mainRep+1 << "," << rep-burnin+1;
            groupIndex=-1;
            for (int i=0; i<n; i++) {
                for (int l=0; l<loci; l++) {
                    for (int p=0; p<ploidy_vec[i]; p++) {
                        groupIndex++;
                        globals.outputPosteriorGrouping_fileStream << "," << linearGroup[groupIndex];
                    }
                }
            }
            globals.outputPosteriorGrouping_fileStream << "\n";
			globals.outputPosteriorGrouping_fileStream.flush();
        }
        
    } // end of MCMC
    
    
    // finish off Qmatrices
    if (fixLabels) {
        
        // finish off gene level Qmatrices
        groupIndex=-1;
        for (int ind=0; ind<n; ind++) {
            for (int l=0; l<loci; l++) {
                for (int p=0; p<ploidy_vec[ind]; p++) {
                    groupIndex++;
                    for (int k=0; k<K; k++) {
                        Qmatrix_gene[groupIndex][k] = exp(logQmatrix_gene[groupIndex][k] - log(double(samples)));
                    } // k
                } // p
            } // l
        } // ind
        
        
        // calculate individual level Qmatrices
        groupIndex=-1;
        for (int ind=0; ind<n; ind++) {
            for (int l=0; l<loci; l++) {
                for (int p=0; p<ploidy_vec[ind]; p++) {
                    groupIndex++;
                    for (int k=0; k<K; k++) {
                        Qmatrix_ind[ind][k] += Qmatrix_gene[groupIndex][k];
                    }
                }
            }
            for (int k=0; k<K; k++) {
                Qmatrix_ind[ind][k] /= double(ploidy_vec[ind]*loci);
            }
        }
        
        // calculate population level Qmatrices
        if (outputQmatrix_pop_on) {
            for (int i=0; i<n; i++) {
                for (int k=0; k<K; k++) {
                    Qmatrix_pop[globals.pop_index[i]][k] += Qmatrix_ind[i][k];
                }
            }
            for (int i=0; i<int(uniquePops.size()); i++) {
                for (int k=0; k<K; k++) {
                    Qmatrix_pop[i][k] /= double(globals.uniquePop_counts[i]);
                }
            }
        }
        
    } // end of if fixLabels
    
    // finish off harmonic mean
    harmonic = log(double(samples))-harmonic;
     
}

//------------------------------------------------
// MCMCobject_admixture::
// resample group allocation of all individuals by drawing from conditional posterior
void MCMCobject_admixture::group_update() {
    
    // update group allocation for this gene copy
    groupIndex=-1;
    for (unsigned int ind=0; ind<n; ind++) {
        for (unsigned int l=0; l<loci; l++) {
            for (unsigned int p=0; p<ploidy_vec[ind]; p++) {
                groupIndex++;
                
                // subtract this gene copy from allele counts and admix counts
                if (data[ind][l][p]!=0) {   // if not missing data
                    alleleCounts[linearGroup[groupIndex]-1][l][data[ind][l][p]-1]--;
                    alleleCountsTotals[linearGroup[groupIndex]-1][l]--;
                    
                    admixCounts[ind][linearGroup[groupIndex]-1]--;
                    admixCountsTotals[ind]--;
                }
                
                // calculate probability of this gene copy from all demes
                probVecSum = 0;
                for (unsigned int k=0; k<K; k++) {
                    if (data[ind][l][p]==0) {
                        probVec[k] = 1.0;
                    } else {
                        probVec[k] = double(alleleCounts[k][l][data[ind][l][p]-1]+lambda)/double(alleleCountsTotals[k][l]+J[l]*lambda);
                        if (beta!=1.0) {
                            probVec[k] = pow(probVec[k],beta);
                        }
                    }
                    probVec[k] *= double(admixCounts[ind][k]+alpha);  // (denominator of this expression is the same for all k, so is omitted)
                    probVecSum += probVec[k];
                }
                
                // resample grouping
                linearGroup[groupIndex] = sample1(probVec, probVecSum);
                
                // add this gene copy to allele counts and admix counts
                if (data[ind][l][p]!=0) {   // if not missing data
                    alleleCounts[linearGroup[groupIndex]-1][l][data[ind][l][p]-1]++;
                    alleleCountsTotals[linearGroup[groupIndex]-1][l]++;
                    
                    admixCounts[ind][linearGroup[groupIndex]-1]++;
                    admixCountsTotals[ind]++;
                }
            } // p
        } // l
    } // ind
    
}

//------------------------------------------------
// MCMCobject_admixture::
// draw allele frequencies and admixture proportions
void MCMCobject_admixture::drawFreqs() {
    
    // draw allele frequencies
    double randSum;
    for (int k=0; k<K; k++) {
        for (int l=0; l<loci; l++) {
            randSum = 0;
            for (int j=0; j<J[l]; j++) {
                alleleFreqs[k][l][j] = rgamma1(alleleCounts[k][l][j]+lambda, 1.0);
                randSum += alleleFreqs[k][l][j];
            }
            for (int j=0; j<J[l]; j++) {
                alleleFreqs[k][l][j] /= randSum;
            }
            
        }
    }
    
    // draw admixture proportions
    for (int i=0; i<n; i++) {
        randSum = 0;
        for (int k=0; k<K; k++) {
            admixFreqs[i][k] = rgamma1(admixCounts[i][k]+alpha, 1.0);
            randSum += admixFreqs[i][k];
        }
        for (int k=0; k<K; k++) {
            admixFreqs[i][k] /= randSum;
        }
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// resample alpha by Metropolis algorithm
void MCMCobject_admixture::alpha_update() {
    
    double alpha_new = rnorm1(alpha,alphaPropSD);
    
    // reflect off boundries at 0 and 10
    if (alpha_new<0 || alpha_new>10) {
        // use multiple reflections to bring into range [-10,+20]
        while (alpha_new< -10)
            alpha_new += 20;
        while (alpha_new> 20)
            alpha_new -= 20;
        
        // use one more reflection to bring into range [0,10]
        if (alpha_new<0)
            alpha_new = -alpha_new;
        if (alpha_new>10)
            alpha_new = 20-alpha_new;
    }
    
    // don't let alpha_new equal exactly 0 (to avoid nan values)
    if (alpha_new==0) {
        alpha_new = pow(10.0,-300.0);
    }
    
    // calculate likelihood under old and new alpha values. Likelihood only derives from admixture proportions - not allele freqencies
    double logProb_old = 0;
    double logProb_new = 0;
    for (int i=0; i<n; i++) {
        logProb_old += lgamma(K*alpha)-lgamma(admixCountsTotals[i]+K*alpha);
        logProb_new += lgamma(K*alpha_new)-lgamma(admixCountsTotals[i]+K*alpha_new);
        for (int k=0; k<K; k++) {
            logProb_old += lgamma(admixCounts[i][k]+alpha)-lgamma(alpha);
            logProb_new += lgamma(admixCounts[i][k]+alpha_new)-lgamma(alpha_new);
        }
    }
    // perform Metropolis step
    if (runif1(0.0,1.0)<exp(logProb_new-logProb_old)) {
        alpha = alpha_new;
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// choose best permutation of labels using method of Stephens (2000)
void MCMCobject_admixture::chooseBestLabelPermutation(globals &globals, int rep) {
    
    // calculate cost matrix from old and new Qmatrices
    for (int k1=0; k1<K; k1++) {
        for (int k2=0; k2<K; k2++) {
            costMat[k1][k2] = 0;
            for (int i=0; i<geneCopies; i++) {
                costMat[k1][k2] += Qmatrix_gene_new[i][k1]*(logQmatrix_gene_new[i][k1]-logQmatrix_gene_running[i][k2]);
            }
        }
    }
    
    // find best permutation of current labels
    bestPerm = hungarian(costMat, edgesLeft, edgesRight, blockedLeft, blockedRight, globals.outputLog_on, globals.outputLog_fileStream);

    // define bestPermOrder. If the numbers 1:m_K are placed in best-perm-order then we arrive back at bestPerm. In R terms we would say bestPermOrder=order(bestPerm).
    bool performSwap = false;
    for (int k=0; k<K; k++) {
        bestPermOrder[bestPerm[k]] = k;
        if (bestPerm[k]!=k)
            performSwap = true;
    }
    
    // swap labels if necessary
    if (performSwap) {
        
        // update grouping to reflect swapped labels
        for (int i=0; i<geneCopies; i++) {
            linearGroup[i] = bestPerm[linearGroup[i]-1]+1;
        }
        
        // update allele counts to reflect swapped labels
        old_alleleCounts = alleleCounts;
        old_alleleCountsTotals = alleleCountsTotals;
        for (int k=0; k<K; k++) {
            alleleCounts[k] = old_alleleCounts[bestPermOrder[k]];
            alleleCountsTotals[k] = old_alleleCountsTotals[bestPermOrder[k]];
        }
        
        // update admix counts to reflect swapped labels
        old_admixCounts = admixCounts;
        for (int i=0; i<n; i++) {
            for (int k=0; k<K; k++) {
                admixCounts[i][k] = old_admixCounts[i][bestPermOrder[k]];
            }
        }
        
        // update logQmatrix_gene_new to reflect swapped labels. No need to swap Qmatrix_gene_new, as this is not used again until after it is recalcualted
        logQmatrix_gene_old = logQmatrix_gene_new;
        for (int i=0; i<geneCopies; i++) {
            for (int k=0; k<K; k++) {
                logQmatrix_gene_new[i][k] = logQmatrix_gene_old[i][bestPermOrder[k]];
            }
        }
        
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// calculate logQmatrix_gene_new for this iteration
void MCMCobject_admixture::produceQmatrix() {
    
    // populate Qmatrix_gene_new
    groupIndex=-1;
    for (unsigned int ind=0; ind<n; ind++) {
        for (unsigned int l=0; l<loci; l++) {
            for (unsigned int p=0; p<ploidy_vec[ind]; p++) {
                groupIndex++;
                
                probVecSum = 0;
                for (unsigned int k=0; k<K; k++) {
                    if (data[ind][l][p]==0) {
                        probVec[k] = 1.0;
                    } else {
                        probVec[k] = double(alleleCounts[k][l][data[ind][l][p]-1]+lambda)/double(alleleCountsTotals[k][l]+J[l]*lambda);
                    }
                    probVec[k] *= double(admixCounts[ind][k]+alpha); // (denominator of this expression is the same for all k, so is omitted)
                    probVecSum += probVec[k];
                }
                for (unsigned int k=0; k<K; k++) {
                    Qmatrix_gene_new[groupIndex][k] = probVec[k]/probVecSum;
                    logQmatrix_gene_new[groupIndex][k] = log(Qmatrix_gene_new[groupIndex][k]);
                }
                
            } // p
        } // l
    } // ind
}

//------------------------------------------------
// MCMCobject_admixture::
// add logQmatrix_gene_new to logQmatrix_gene_running
void MCMCobject_admixture::updateQmatrix(int rep) {
    
    // add logQmatrix_gene_new to logQmatrix_gene_running
    for (int i=0; i<geneCopies; i++) {
        for (int k=0; k<K; k++) {
            logQmatrix_gene_running[i][k] = logSum(logQmatrix_gene_running[i][k], logQmatrix_gene_new[i][k]);
        }
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// store Qmatrix values
void MCMCobject_admixture::storeQmatrix() {
    
    // store individual-level Qmatrix
    groupIndex=-1;
    for (int ind=0; ind<n; ind++) {
        for (int l=0; l<loci; l++) {
            for (int p=0; p<ploidy_vec[ind]; p++) {
                groupIndex++;
                for (int k=0; k<K; k++) {
                    logQmatrix_gene[groupIndex][k] = logSum(logQmatrix_gene[groupIndex][k], logQmatrix_gene_new[groupIndex][k]);
                }
            }
        }
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// probability of data given grouping only, integrated over unknown allele frequencies
void MCMCobject_admixture::d_logLikeGroup() {
    
    // Multinomial-Dirichlet likelihood
    logLikeGroup = 0;
    for (int k=0; k<K; k++) {
        for (int l=0; l<loci; l++) {
            for (int j=0; j<J[l]; j++) {
                logLikeGroup += lgamma(lambda + alleleCounts[k][l][j]) - lgamma(lambda);
            }
            logLikeGroup += lgamma(J[l]*lambda) - lgamma(J[l]*lambda + alleleCountsTotals[k][l]);
        }
    }
    
}

//------------------------------------------------
// MCMCobject_admixture::
// probability of data given grouping and known allele frequencies and admixture proportions
void MCMCobject_admixture::d_logLikeJoint() {
    
    // calculate likelihood
    logLikeJoint = 0;
    double temp1;
    for (int i=0; i<n; i++) {
        for (int l=0; l<loci; l++) {
            for (int p=0; p<ploidy_vec[i]; p++) {
                if (data[i][l][p]!=0) {
                    temp1 = 0;
                    for (int k=0; k<K; k++) {
                        temp1 += admixFreqs[i][k]*alleleFreqs[k][l][data[i][l][p]-1];
                    }
                    logLikeJoint += log(temp1);
                }
            }
        }
    }
    
}

