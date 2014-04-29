#pragma once
#include "SparseMatrixCV\SparseMatrixCV.h" 
#include "opencv2\core\core.hpp"
using namespace cv; 

class Line3D; 
template <typename T> class Data3D; 
template <typename T> class ModelSet; 

extern const double LOGLIKELIHOOD;
extern const double PAIRWISESMOOTH; 

class LevenburgMaquart
{
	// projections of datapoints & 
	// the Jacobain matrix of the projections
	vector<Vec3d> P;
	vector<SparseMatrixCV> nablaP;

	const vector<Vec3i>& dataPoints;
	const vector<int>& labelings;
	const ModelSet<Line3D>& modelset;
	const Data3D<int>& labelIDs;
public:
	LevenburgMaquart( const vector<Vec3i>& dataPoints, const vector<int>& labelings, 
		const ModelSet<Line3D>& modelset, const Data3D<int>& labelIDs ) 
		: dataPoints( dataPoints ), labelings( labelings )
		, modelset( modelset ), labelIDs( labelIDs ) { }

	void reestimate( void ); 
private:
	//// Jacobian Matrix - data cost
	void datacost_jacobian(
		vector<double>& Jacobian_nzv, 
		vector<int>&    Jacobian_colindx, 
		vector<int>&    Jacobian_rowptr,
		vector<double>& energy_matrix );
	
	// Jacobian Matrix - smooth cost
	void Jacobian_smoothcost( 
		vector<double>& Jacobian_nzv, 
		vector<int>&    Jacobian_colindx,  
		vector<int>&    Jacobian_rowptr, 
		vector<double>& energy_matrix );
	// Jacobian Matrix - smooth cost
	void Jacobian_smoothcost_openmp( 
		vector<double>& Jacobian_nzv, 
		vector<int>&    Jacobian_colindx,  
		vector<int>&    Jacobian_rowptr, 
		vector<double>& energy_matrix );
	void Jacobian_smoothcost_openmp_critical_section(
		vector<double>& Jacobian_nzv, 
		vector<int>&    Jacobian_colindx,  
		vector<int>&    Jacobian_rowptr, 
		vector<double>& energy_matrix );
};

