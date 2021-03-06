#include "RingsReduction.h"

#include <opencv2/opencv.hpp>
#include <iostream>
#include <omp.h>

#include "Interpolation.h"
#include "ImageProcessing.h"
#include "CVPlot.h"

using namespace cv;
using namespace std;



double RingsReduction::max_ring_radius( const cv::Vec2d& center,
                                        const cv::Vec2d& im_size )
{
    // four relative corner with respect to the centre of rings
    const Vec2d corner[4] =
    {
        Vec2d(0          - center[0], 0          - center[1]),
        Vec2d(im_size[0] - center[0], im_size[1] - center[1]),
        Vec2d(0          - center[0], im_size[1] - center[1]),
        Vec2d(im_size[0] - center[0], 0          - center[1])
    };

    // maximum possible radius of the rings
    double max_radius_squre = 0;
    for( int i=0; i<4; i++ )
    {
        double current = corner[i][0]*corner[i][0] + corner[i][1]*corner[i][1];
        max_radius_squre = max( max_radius_squre, current );
    }
    return sqrt( max_radius_squre );
}


double RingsReduction::avgI_on_rings( const cv::Mat_<short>& m,
                                      const cv::Vec2d& ring_center,
                                      const int& rid,
                                      const double& dr )
{
    smart_assert( dr>0, "dr indicates the thickness of the rings, \
                 which should be greater than 0. " );

    // sum of intensities
    double sumI = 0.0;

    // number of pixels
    double pixel_count = 0;

    // center of the ring
    const double& center_x = ring_center[0];
    const double& center_y = ring_center[1];

    // image size
    const int& im_size_x = m.rows;
    const int& im_size_y = m.cols;

    // the range of the ring is [r_min,r_max], any pixels falls between this
    // range are considered as part of the ring
    double radius = rid * dr;
    const double r_min = radius - dr;
    const double r_max = radius + dr;
    const double r_min2 = r_min * r_min;
    const double r_max2 = r_max * r_max;

    // (x,y) pixel position with respect the center of the ring
    for( double x = 1; x<=r_max; x++ )
    {
        const double x2 = x * x;

        const double y_min = sqrt( std::max(0.0, r_min2 - x2) );
        const double y_max = sqrt( r_max2 - x2 );

        for( double y=std::max(y_min, 1.0); y<=y_max; y++ )
        {
            const double y2 = y * y;

            // distance from the current point to the centre of the ring
            const double pixel_radius = sqrt( x2 + y2 );

            const double dist_2_ring = abs( pixel_radius - radius );

            if( dist_2_ring>dr ) continue;

            const double percentage = 1.0 - dist_2_ring/dr;

            // 4 Quadrants
            for( int quadrant = 0; quadrant < 4; quadrant++ )
            {

                const int pixel_x = int( center_x + x * ((quadrant&&1)*2-1) );
                const int pixel_y = int( center_y + y * ((quadrant>>1)*2-1) );

                if( pixel_x >= 0 && pixel_x < im_size_x
                        && pixel_y >= 0 && pixel_y < im_size_y )
                {
                    sumI += m(pixel_y, pixel_x) * percentage;
                    pixel_count += percentage;
                }
            }
        }
    }

    // along 4 axis
    for( int i=0; i<4; i++ )
    {
        static const int aoffset[4][2] =
        {
            {-1, 0}, { 0,-1}, { 1, 0}, { 0, 1}
        };
        const int pixel_x = int( center_x + radius * aoffset[i][0] );
        const int pixel_y = int( center_y + radius * aoffset[i][1] );

        if( pixel_x >= 0 && pixel_x < im_size_x
                && pixel_y >= 0 && pixel_y < im_size_y )
        {
            sumI += m(pixel_y, pixel_x);
            pixel_count += 1.0;
        }
    }

    if( pixel_count > 1e-2 ) return sumI / pixel_count;
    else return 0.0;
}

void RingsReduction::sijbers( const Data3D<short>& src, Data3D<short>& dst,
                              const double& dr,
                              const Vec2d& ring_centre,
                              bool isGaussianBlur,
                              std::vector<double>* pCorrection )
{
    const int wsize = 15;

    if( &dst!=&src)
    {
        dst.resize( src.get_size() );
    }

    // Blur the image
    cout << "Blurring the image... ";
    cout.flush();
    Data3D<short> mean( src.get_size() );
    if( isGaussianBlur )
    {
        IP::GaussianBlur3D( src, mean, 2*wsize+1 );
    }
    else
    {
        IP::meanBlur3D( src, mean, wsize );
    }
    cout << "Done. " << endl;

    Data3D<short>& diff = mean;
    subtract3D( src, mean, diff );

    /// TODO: Uncomment the following code if you want to use variance
    //Data3D<int> variance_sum( im.get_size() );
    //multiply3D(diff, diff, variance_sum);
    //Data3D<int> variance( im.get_size() );
    //IP::meanBlur3D( variance_sum, variance, wsize );

    const Vec2d im_size( (double) src.SX(), (double) src.SY() );

    const double max_radius = max_ring_radius( ring_centre, im_size );

    const unsigned num_of_rings = unsigned( max_radius / dr );

    vector<double> correction( num_of_rings, 0 );
    for( int z=0; z<src.SZ(); z++ )
    {
        // rings reduction is done slice by slice here
        cout << '\r' << "Rings Reduction: " << 100 * z / src.SZ() << "%";
        cout.flush();

        const Mat_<short> m = diff.getMat(z);

        #pragma omp parallel for schedule(dynamic)
        for( unsigned ri = 0; ri<num_of_rings-1; ri++ )
        {
            correction[ri] = med_on_ring( m, ring_centre, ri, dr );
        }

        correct_image( src, dst, correction, z, ring_centre, dr );
        if( pCorrection ) *pCorrection = correction;
    }
    cout << endl;
}

void RingsReduction::correct_image( const cv::Mat_<short>& src,
                                    cv::Mat_<short>& dst,
                                    const std::vector<double>& correction,
                                    const cv::Vec2d& ring_center,
                                    const double& dradius )
{
    dst = Mat_<short>(src.rows, src.cols);

    for( int y=0; y<src.rows; y++ )
    {
        for( int x=0; x<src.cols; x++ )
        {

            const double diff_x = x - ring_center[0];
            const double diff_y = y - ring_center[1];
            const double radius = sqrt( diff_x*diff_x + diff_y*diff_y );

            /* For any rid that bigger than the size of the correction vector,
               pretend that it is the most outer ring (rid = correction.size()-1).
               This assumption may
               result in a few of the pixels near the corner of the image corner
               being ignored. But it will prevent the function from crashing if
               it was not used properly. */
            const double rid = min( radius/dradius, (double) correction.size()-1 );

            const int flo = (int) std::floor( rid );
            const int cei = (int) std::ceil( rid );
            double c = 0;
            if( flo!=cei )
            {
                c = correction[flo] * ( cei - rid ) +
                    correction[cei] * ( rid - flo );
            }
            else
            {
                c = correction[flo];
            }
            dst(y, x) = short( src(y,x) - c );
        }
    }
}

void RingsReduction::correct_image( const Data3D<short>& src,
                                    Data3D<short>& dst,
                                    const vector<double>& correction,
                                    const int& slice,
                                    const Vec2d& ring_center,
                                    const double& dr )
{
    if( dst.get_size()!=src.get_size() )
        dst.reset( src.get_size(), short(0) );

    const int& z = slice;
    for( int x=0; x<src.SX(); x++ )
    {
        for( int y=0; y<src.SY(); y++ )
        {

            const double diff_x = x - ring_center[0];
            const double diff_y = y - ring_center[1];
            const double radius = sqrt( diff_x*diff_x + diff_y*diff_y );

            /* For any rid that bigger than the size of the correction vector,
               pretend that it is the most outer ring (rid = correction.size()-1).
               This assumption may
               result in a few of the pixels near the corner of the image corner
               being ignored. But it will prevent the function from crashing if
               it was not used properly. */
            const double rid = min( radius/dr, (double) correction.size()-1 );

            const int flo = (int) std::floor( rid );
            const int cei = (int) std::ceil( rid );
            double c = 0;
            if( flo!=cei )
            {
                c = correction[flo] * ( cei - rid ) +
                    correction[cei] * ( rid - flo );
            }
            else
            {
                c = correction[flo];
            }
            dst.at(x,y,z) = short( src.at(x,y,z) - c );
        }
    }
}


void RingsReduction::polarRD( const Data3D<short>& src, Data3D<short>& dst,
                              const PolarRDOption& o, const double dr,
                              const Vec2d& approx_centre,
                              const double& subpixel_on_ring,
                              vector<double>* pCorrection )
{
    smart_assert( &src!=&dst,
                  "The destination file is the same as the original. " );

    // TODO: do it on a 3D volume
    const int center_z = src.SZ() / 2;

    const Vec2d& ring_center = approx_centre;

    const Vec2d im_size( (double)src.SX(), (double)src.SY() );

    const double max_radius = max_ring_radius( ring_center, im_size );

    const int num_of_rings = int( max_radius / dr );

    double (*diff_func)(const cv::Mat_<short>&, const cv::Vec2d&,
                        const int&, const int&,
                        const double&, const double& ) = nullptr;
    switch (o )
    {
    case AVG_DIFF:
        diff_func = &avg_diff_v2;
        break;
    case MED_DIFF:
        diff_func = &med_diff_v2;
        break;
    default:
        cerr << "Undefined method option. " << endl;
        break;
    }

    /* The intensity of this ring is not supposed to be alter, that is,
       correction[const_ri] = 0*/
    const int const_ri = int( 100/dr );

    // compute correction vector
    vector<double> correction( num_of_rings, 0 );
    for( int ri = 0; ri<num_of_rings-1; ri++ )
    {
        correction[ri] = diff_func( src.getMat(center_z), ring_center,
                                    ri, const_ri, dr, subpixel_on_ring );
    }

    correct_image( src, dst, correction, center_z, ring_center, dr );

    if( pCorrection!=nullptr ) *pCorrection = correction;
}



void RingsReduction::MMDPolarRD( const Mat_<short>& src, Mat_<short>& dst,
                                 const Vec2d& ring_center,
                                 const double dradius )
{
    smart_assert( &src!=&dst, "The destination file should not be the same as the original. " );

    const Vec2d im_size( (double)src.rows, (double)src.cols );

    const double max_radius = max_ring_radius( ring_center, im_size );

    const unsigned num_of_rings = unsigned( max_radius / dradius );

    // compute correction vector
    vector<double> correction( num_of_rings, 0 );

    #pragma omp parallel for schedule(dynamic)
    for( unsigned ri = 0; ri<num_of_rings-1; ri++ )
    {
        correction[ri] = med_diff( src, ring_center, ri, ri+1, dradius );
    }

    // accumulate the correction vector
    for( int ri = num_of_rings-2; ri>=0; ri-- )
    {
        correction[ri] += correction[ri+1];
    }

    /* The intensity of this ring is not supposed to be alter, that is,
       correction[int( 100/dr )] = 0*/
    const double drift = correction[ int(100/dradius) ];
    for( unsigned ri = 0; ri<num_of_rings; ri++ )
    {
        correction[ri] -= drift;
    }

    correct_image( src, dst, correction, ring_center, dradius );
}


void RingsReduction::MMDPolarRD( const Data3D<short>& src,
                                 Data3D<short>& dst,
                                 const cv::Vec2d& first_slice_centre,
                                 const cv::Vec2d& last_slice_centre,
                                 const double dradius )
{
    smart_assert( &src!=&dst, "The destination file is the same as the original. " );

    const Vec2d im_size( (double)src.SX(), (double)src.SY() );

    const double max_radius = std::max(
                                  max_ring_radius( first_slice_centre, im_size ),
                                  max_ring_radius( last_slice_centre,  im_size ));

    const unsigned num_of_rings = unsigned( max_radius / dradius );

    #pragma omp parallel
    {
        vector<double> correction( num_of_rings, 0 );

        #pragma omp for// schedule(dynamic)// private(correction)
        for( int z = 0; z<src.SZ(); z++ )
        {
            std::fill( correction.begin(), correction.end(), 0);

            const Vec2d ring_center = ( double(z)*first_slice_centre + double(src.SZ()-z-1)*last_slice_centre ) / (src.SZ()-1);

            const Mat_<short> m = src.getMat(z);

            for( unsigned ri = 0; ri<num_of_rings-1; ri++ )
            {
                correction[ri] = med_diff( m, ring_center, ri, ri+1, dradius );
            }

            // accumulate the correction vector
            for( int ri = num_of_rings-2; ri>=0; ri-- )
            {
                correction[ri] += correction[ri+1];
            }

            /* The intensity of this ring is not supposed to be alter, that is,
               correction[int( 100/dr )] = 0*/
            const double drift = correction[ int(100/dradius) ];
            for( unsigned ri = 0; ri<num_of_rings; ri++ )
            {
                correction[ri] -= drift;
            }

            #pragma omp critical (dst)
            {
                correct_image( src, dst, correction, z, ring_center, dradius );
            }
        }
    }
}


double RingsReduction::avg_diff( const cv::Mat_<short>& m,
                                 const cv::Vec2d& ring_center,
                                 const int& rid1,
                                 const int& rid2,
                                 const double& dradius )
{
    // radius of the two circles
    const double radius  = rid1 * dradius;
    const double radius1 = rid2 * dradius;

    // the number of pixels on the circumference approximately
    const double bigger = std::max( radius, radius1 );
    const int circumference = std::max( 8, int( 2 * M_PI * bigger ) );

    double sum = 0.0;
    int count = 0;

    const double dangle = 2 * M_PI / circumference;
    const double dangle_2 = dangle / 2;
    const double dradius_2 = dradius / 2;

    for( int i=0; i<circumference; i++ )
    {
        // angle in radian
        const double angle = i * dangle;

        const double sin_angle = sin( angle );
        const double cos_angle = cos( angle );

        // image position for inner circle
        const Vec2d pos( radius * cos_angle + ring_center[0],
                         radius * sin_angle + ring_center[1] );

        // image position for outer circle
        const Vec2d pos1( radius1 * cos_angle + ring_center[0],
                          radius1 * sin_angle + ring_center[1] );

        if( Interpolation<short>::isvalid(m, pos) && Interpolation<short>::isvalid(m, pos1) )
        {
            const double val  = Interpolation<short>::Get( m, pos,  ring_center, dangle_2, dradius_2 );
            const double val1 = Interpolation<short>::Get( m, pos1, ring_center, dangle_2, dradius_2 );
            sum += val - val1;
            count++;
        }
    }

    return (count>0) ? sum/count : 0;
}

double RingsReduction::med_diff( const cv::Mat_<short>& m,
                                 const cv::Vec2d& ring_center,
                                 const int& rid1,
                                 const int& rid2,
                                 const double& dradius )
{
    // radius of the two circles
    const double radius  = rid1 * dradius;
    const double radius1 = rid2 * dradius;

    // the number of pixels on the circumference approximately
    const double bigger = std::max( radius, radius1 );
    const int circumference = std::max( 8, int( 2 * M_PI * bigger ) );

    std::vector<double> diffs(1, 0);

    const double dangle = 2 * M_PI / circumference;
    const double dangle_2 = dangle / 2;
    const double dradius_2 = dradius / 2;

    for( int i=0; i<circumference; i++ )
    {
        // angle in radian
        const double angle = i * dangle;
        const double sin_angle = sin( angle );
        const double cos_angle = cos( angle );

        // image position for inner circle
        const double x = radius * cos_angle + ring_center[0];
        const double y = radius * sin_angle + ring_center[1];

        // image position for outer circle
        const double x1 = radius1 * cos_angle + ring_center[0];
        const double y1 = radius1 * sin_angle + ring_center[1];

        if( Interpolation<short>::isvalid( m, x1, y1) && Interpolation<short>::isvalid( m, x, y) )
        {
            const double val  = Interpolation<short>::Get( m, Vec2d(x,y),   ring_center, dangle_2, dradius_2 );
            const double val1 = Interpolation<short>::Get( m, Vec2d(x1,y1), ring_center, dangle_2, dradius_2 );
            diffs.push_back( val - val1 );
        }
    }

    return median( diffs );
}

double RingsReduction::avg_diff_v2( const cv::Mat_<short>& m,
                                    const cv::Vec2d& ring_center,
                                    const int& rid1,
                                    const int& rid2,
                                    const double& dr,
                                    const double& subpixel_on_ring )
{
    const double avg1 = avg_on_ring( m, ring_center, rid1, dr, subpixel_on_ring );
    const double avg2 = avg_on_ring( m, ring_center, rid2, dr, subpixel_on_ring );
    return avg1 - avg2;
}


double RingsReduction::med_diff_v2( const cv::Mat_<short>& m,
                                    const cv::Vec2d& ring_center,
                                    const int& rid1,
                                    const int& rid2,
                                    const double& dr,
                                    const double& subpixel_on_ring )
{
    const double med1 = med_on_ring( m, ring_center, rid1, dr, subpixel_on_ring );
    const double med2 = med_on_ring( m, ring_center, rid2, dr, subpixel_on_ring );
    return med1 - med2;
}



double RingsReduction::avg_on_ring( const cv::Mat_<short>& m,
                                    const cv::Vec2d& ring_center,
                                    const int& rid, const double& dradius,
                                    const double& subpixel_on_ring )
{
    // radius of the circle
    const double radius = rid * dradius;

    // the number of pixels on the circumference approximately
    const int circumference = max( 8, int( 2 * M_PI * radius / subpixel_on_ring ) );

    int count = 0;
    double sum = 0.0;

    const double dangle = 2 * M_PI / circumference;
    const double dangle_2 = dangle / 2;
    const double dradius_2 = dradius / 2;
    for( int i=0; i<circumference; i++ )
    {
        // angle in radian
        const double angle = i * dangle;
        const double sin_angle = sin( angle );
        const double cos_angle = cos( angle );

        // image position
        const double x = radius * cos_angle + ring_center[0];
        const double y = radius * sin_angle + ring_center[1];

        if( Interpolation<short>::isvalid(m, x, y) )
        {
            sum += Interpolation<short>::Get( m, Vec2d(x,y), ring_center, dangle_2, dradius_2 );
            count++;
        }
    }

    return (count>0) ? sum/count : 0;
}



double RingsReduction::median( std::vector<double>& values )
{
    if( values.size()==0 ) return 0.0;

    std::sort( values.begin(), values.end() );

    const double mid_id = 0.5 * (double) values.size();

    const int id1 = (int) std::floor( mid_id );
    const int id2 = (int) std::ceil(  mid_id );

    return 0.5 * ( values[id1] + values[id2] );
}

std::vector<double> RingsReduction::distri_of_diff( const cv::Mat_<short>& m,
        const cv::Vec2d& ring_center,
        const int& rid1, const int& rid2,
        const double& dradius )
{
    // radius of the two circles
    const double radius  = rid1 * dradius;
    const double radius1 = rid2 * dradius;

    // the number of pixels on the circumference approximately
    const double bigger = std::min( radius, radius1 );
    const int circumference = std::max( 8, int( 2 * M_PI * bigger ) );

    std::vector<double> diffs;

    const double dangle = 2 * M_PI / circumference;
    const double dangle_2 = dangle / 2;
    const double dradius_2 = dradius / 2;

    for( int i=0; i<circumference; i++ )
    {
        // angle in radian
        const double angle = i * dangle;
        const double sin_angle = sin( angle );
        const double cos_angle = cos( angle );

        // image position for inner circle
        const double x = radius * cos_angle + ring_center[0];
        const double y = radius * sin_angle + ring_center[1];

        // image position for outer circle
        const double x1 = radius1 * cos_angle + ring_center[0];
        const double y1 = radius1 * sin_angle + ring_center[1];

        if( Interpolation<short>::isvalid(m, x1, y1) && Interpolation<short>::isvalid(m, x, y) )
        {
            const double val  = Interpolation<short>::Get( m, Vec2d(x, y ), ring_center, dangle_2, dradius_2 );
            const double val1 = Interpolation<short>::Get( m, Vec2d(x1,y1), ring_center, dangle_2, dradius_2 );
            diffs.push_back( val-val1 );
        }
    }

    // sort the difference vector
    std::sort( diffs.begin(), diffs.end() );

    const unsigned num_of_bins = 200; // TODO: Can make this a parameter
    vector<double> bins(num_of_bins, 0);

    const double minVal = diffs.front();
    const double maxVal = diffs.back();
    const double diff_range = maxVal - minVal;

    for( unsigned i=0; i<diffs.size(); i++ )
    {
        unsigned binid = (unsigned) (num_of_bins * (diffs[i] - minVal) / diff_range);
        binid = std::min( binid, num_of_bins-1);
        bins[ binid ]++;
    }

    return bins;
}





//
//
//// helper structure
//typedef struct
//{
//    short diff;
//    int var;
//} Diff_Var;
//
//// helper fucntion
//short get_reduction( Diff_Var* diff_var, int count )
//{
//    ////////////////////////////
//    //// Uncomment the following code if you want to use variance
//    //// sort the data based on var
//    //for( int i=0; i<count; i++ ){
//    //	for( int j=i+1; j<count; j++ ){
//    //		if( diff_var[i].var > diff_var[j].var )
//    //			std::swap( diff_var[i], diff_var[j] );
//    //	}
//    //}
//    //// discard 40% of the data (with big variances)
//    // count = std::min( count, std::max( 20, count*4/5 ));
//
//    // sort the data based on diff
//    for( int i=0; i<count; i++ )
//    {
//        for( int j=i+1; j<count; j++ )
//        {
//            if( diff_var[i].diff > diff_var[j].diff )
//                std::swap( diff_var[i], diff_var[j] );
//        }
//    }
//    return diff_var[count/2].diff;
//}
//
//


//
//void RingsReduction::mm_filter( const Data3D<short>& im_src, Data3D<short>& dst )
//{
//
//    // TODO: set the center as parameters
//    const int center_x = 234.0;
//    const int center_y = 270;
//    const int wsize = 15;
//
//
//    if( &dst!=&im_src)
//    {
//        dst.resize( im_src.get_size() );
//    }
//
//    Data3D<short> mean( im_src.get_size() );
//    IP::meanBlur3D( im_src, mean, wsize );
//    //mean.save( "temp.mean.data" );
//    //mean.load( "temp.mean.data" );
//    //mean.show( "rd mean" );
//
//    Data3D<int> diff( im_src.get_size() );
//    subtract3D(im_src, mean, diff);
//    //diff.save( "temp.diff.data", "rings reduction intermedia result -
//    //									Mean Blur with sigma = 19. Differnce between
//    //									original data. " );
//    //diff.load( "temp.diff.data" );
//    //diff.show( "rd diff" );
//
//    //// Uncomment the following code if you want to use variance
//    //Image3D<int> variance_sum( im.get_size() );
//    //multiply3D(diff, diff, variance_sum);
//    //Image3D<int> variance( im.get_size() );
//    //IP::meanBlur3D( variance_sum, variance, wsize );
//
//    // four relative corner with respect to the centre of rings
//    Vec2i offsets[4] =
//    {
//        Vec2i(0                 -center_x, 0                 -center_y),
//        Vec2i(im_src.get_size(0)-center_x, im_src.get_size(1)-center_y),
//        Vec2i(0                 -center_x, im_src.get_size(1)-center_y),
//        Vec2i(im_src.get_size(0)-center_x, 0                 -center_y)
//    };
//    // maximum possible radius of the rings
//    int max_radius = 0;
//    for( int i=0; i<4; i++ )
//    {
//        max_radius = max( max_radius, offsets[i][0]*offsets[i][0] + offsets[i][1]*offsets[i][1]);
//    }
//    max_radius = int( sqrt(1.0*max_radius) );
//
//    Diff_Var* diff_var = new Diff_Var[ int(4*M_PI*max_radius) ];
//
//    // the ring reduction map: indicate whether a ring is stronger or weaker
//    short* rdmap = new short[ max_radius ];
//    memset( rdmap, 0, sizeof(short)*max_radius );
//
//    const Vec3i& src_size = im_src.get_size();
//    int x, y, z, r;
//    for( z=0; z<src_size[2]; z++ )
//    {
//        // rings reduction is done slice by slice
//        cout << '\r' << "Rings Reduction: " << 100 * z / src_size[2] << "%";
//        cout.flush();
//
//        rdmap[0] = diff.at(center_x, center_y, z);
//        for( r=1; r<max_radius; r++ )
//        {
//            int count = 0;
//            int r_min = r-1;
//            int r_max = r+1;
//            int r_min_square = r_min * r_min;
//            int r_max_square = r_max * r_max;
//            for( int x=1; x<=r_max; x++ )
//            {
//                int x_square = x * x;
//                int y_min = int( ceil(  sqrt( max(0.0, 1.0*r_min_square - x_square) ) ));
//                int y_max = int( floor( sqrt( 1.0*r_max_square - x_square ) ) );
//                for( int y=max(y_min, 1); y<=y_max; y++ )
//                {
//                    int offset_x, offset_y;
//                    // Quandrant 1
//                    offset_x = center_x+x;
//                    offset_y = center_y+y;
//                    if( offset_x>=0 && offset_x<src_size[0] && offset_y>=0 && offset_y<src_size[1] )
//                    {
//                        diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                        //// Uncomment the following code if you want to use variance
//                        //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                        count++;
//                    }
//                    // Quandrant 2
//                    offset_x = center_x-x;
//                    offset_y = center_y+y;
//                    if( offset_x>=0 && offset_x<src_size[0] && offset_y>=0 && offset_y<src_size[1] )
//                    {
//                        diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                        //// Uncomment the following code if you want to use variance
//                        //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                        count++;
//                    }
//                    // Quandrant 3
//                    offset_x = center_x+x;
//                    offset_y = center_y-y;
//                    if( offset_x>=0 && offset_x<src_size[0] && offset_y>=0 && offset_y<src_size[1] )
//                    {
//                        diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                        //// Uncomment the following code if you want to use variance
//                        //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                        count++;
//                    }
//                    // Quandrant 4
//                    offset_x = center_x-x;
//                    offset_y = center_y-y;
//                    if( offset_x>=0 && offset_x<src_size[0] && offset_y>=0 && offset_y<src_size[1] )
//                    {
//                        diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                        //// Uncomment the following code if you want to use variance
//                        //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                        count++;
//                    }
//                }
//            }
//            for( int i=max(r_min, 1) ; i<=r_max; i++ )
//            {
//                int offset_x, offset_y;
//                // y > 0
//                offset_x = center_x;
//                offset_y = center_y+i;
//                if( offset_y>=0 && offset_y<src_size[1] )
//                {
//                    diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                    //// Uncomment the following code if you want to use variance
//                    //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                    count++;
//                }
//                // x > 0
//                offset_x = center_x+i;
//                offset_y = center_y;
//                if( offset_x>=0 && offset_x<src_size[0] )
//                {
//                    diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                    //// Uncomment the following code if you want to use variance
//                    //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                    count++;
//                }
//                // y < 0
//                offset_x = center_x;
//                offset_y = center_y-i;
//                if( offset_y>=0 && offset_y<src_size[1] )
//                {
//                    diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                    //// Uncomment the following code if you want to use variance
//                    //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                    count++;
//                }
//                // x < 0
//                offset_x = center_x-i;
//                offset_y = center_y;
//                if( offset_x>=0 && offset_x<src_size[0] )
//                {
//                    diff_var[count].diff = diff.at(offset_x, offset_y, z);
//                    //// Uncomment the following code if you want to use variance
//                    //diff_var[count].var = variance.at(offset_x, offset_y, z);
//                    count++;
//                }
//            }
//
//            rdmap[r] = get_reduction( diff_var, count );
//        } // end of loop r
//
//        // remove ring for slice z
//        for( y=0; y<src_size[1]; y++ )
//        {
//            for( x=0; x<src_size[0]; x++ )
//            {
//                // relative possition to the center of the ring
//                int relative_x = x - center_x;
//                int relative_y = y - center_y;
//                if( relative_x==0 && relative_y==0 )
//                {
//                    // center of the ring
//                    dst.at(x,y,z) = mean.at(x,y,z);
//                    continue;
//                }
//                // radius of the ring
//                float r = sqrt( float(relative_x*relative_x+relative_y*relative_y) );
//                int floor_r = (int) floor(r);
//                int ceil_r = (int) ceil(r);
//                if( floor_r==ceil_r )
//                {
//                    dst.at(x,y,z) = im_src.at(x,y,z) - rdmap[ceil_r];
//                }
//                else
//                {
//                    dst.at(x,y,z) = im_src.at(x,y,z) - short( rdmap[ceil_r] * (r-floor_r) );
//                    dst.at(x,y,z) = im_src.at(x,y,z) - short( rdmap[floor_r] * (ceil_r-r) );
//                }
//            }
//        }
//    }
//    cout << endl;
//
//    delete[] rdmap;
//    delete[] diff_var;
//}
